/*
 * Copyright (C) 2025 Integrated Solutions (https://www.solutionsdx.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Contributors:
 * Ardavan Hashemzadeh
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <atomic>
#include <afxwin.h>
#include <json.h>
#include <settings.h>
#include <microsip.h>

#pragma comment(lib, "ws2_32.lib")

std::atomic<bool> g_server_cancelled(false);

int get_available_port() {
    std::cout << "[DEBUG] Seeking available port: " << std::endl;
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in service{};
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr("127.0.0.1");
    service.sin_port = 0;

    bind(sock, (SOCKADDR*)&service, sizeof(service));

    sockaddr_in assigned{};
    int len = sizeof(assigned);
    getsockname(sock, (SOCKADDR*)&assigned, &len);

    int port = ntohs(assigned.sin_port);
    closesocket(sock);
    WSACleanup();
    return port;
}

std::string start_one_shot_server(int port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    bind(server, (SOCKADDR*)&addr, sizeof(addr));
    listen(server, 1);

    std::string request;
    std::string body;

    while (!g_server_cancelled) {
        std::cout << "Waiting for a connection on http://localhost:" << port << std::endl;
        SOCKET client = accept(server, nullptr, nullptr);

        char buffer[8192];
        int bytesReceived = recv(client, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            closesocket(client);
            continue; // wait for the next valid connection
        }

        request = std::string(buffer, bytesReceived);

        if (request.find("OPTIONS") == 0) {
            std::string optionsResponse =
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n"
                "Content-Length: 0\r\n"
                "\r\n";
            send(client, optionsResponse.c_str(), optionsResponse.size(), 0);
            closesocket(client);
            continue; // wait for the real request
        }

        size_t bodyPos = request.find("\r\n\r\n");
        body = bodyPos != std::string::npos ? request.substr(bodyPos + 4) : "";

        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" + body;

        send(client, response.c_str(), response.size(), 0);
        closesocket(client);
        break; // exit after one valid request
    }

    closesocket(server);
    WSACleanup();

    return body;
}

void json_oauth_provisioning(std::string provisioning_endpoint, std::string provisioning_environment, std::string provisioning_logout_url, CString iniFile) {
    CmicrosipApp* pApp = (CmicrosipApp*)AfxGetApp();
    CString cmdLine = pApp->m_lpCmdLine;
    bool cmdExit = lstrcmp(cmdLine, _T("/exit")) == 0;
    if (cmdExit) {
        return;
    }
    // Start a one-shot server and get JSON payload
    int port = get_available_port();
    if (port <= 0) {
        AfxMessageBox(_T("Failed to find an available port for provisioning server."));
        return;
    }
    else {
        std::string provisioning_full_url = provisioning_endpoint + "?port=" + std::to_string(port);
        std::string cmd = "start \" \" \"" + provisioning_logout_url + "\" & start \" \" \"" + provisioning_full_url + "&env=" + provisioning_environment + "\"";
        system(cmd.c_str());
    }

    std::string jsonPayload = start_one_shot_server(port);

    // Show the body in a message box
    // MessageBoxA(NULL, jsonPayload.c_str(), "Received Payload", MB_OK | MB_ICONINFORMATION);

    // Parse JSON and set fields
    Json::Value root;
    Json::Reader reader;
    CString section = _T("Account");

    if (reader.parse(jsonPayload, root)) {
        // --- Accounts ---
        if (root.isMember("accounts") && root["accounts"].isArray()) {
            const Json::Value& accounts = root["accounts"];
            for (size_t i = 0; i < accounts.size(); ++i) {
                CString section;
                section.Format(_T("Account%d"), i + 1);
                const Json::Value& acc = accounts[i];

                auto writeField = [&](const char* jsonKey, const TCHAR* iniKey) {
                    if (acc.isMember(jsonKey)) {
                        CString value(acc[jsonKey].asCString());
                        WritePrivateProfileString(section, iniKey, value, iniFile);
                    }
                    };

                writeField("label", _T("label"));
                writeField("server", _T("server"));
                writeField("proxy", _T("proxy"));
                writeField("domain", _T("domain"));
                writeField("username", _T("username"));
                writeField("displayName", _T("displayName"));
                writeField("voicemailNumber", _T("voicemailNumber"));
                writeField("transport", _T("transport"));

                if (acc.isMember("password")) {
                    CString password(acc["password"].asCString());
                    CString encrypted = IniEncrypt(password);
                    WritePrivateProfileString(section, _T("password"), encrypted, iniFile);
                }
                if (acc.isMember("voicemailPassword")) {
                    CString vmpass(acc["voicemailPassword"].asCString());
                    CString encrypted = IniEncrypt(vmpass);
                    WritePrivateProfileString(section, _T("voicemailPassword"), encrypted, iniFile);
                }
            }
        }

        // --- Settings ---
        if (root.isMember("settings") && root["settings"].isObject()) {
            CString section = _T("Settings");
            const Json::Value& settings = root["settings"];
            for (Json::ValueConstIterator it = settings.begin(); it != settings.end(); ++it) {
                CString key(it.key().asCString());
                CString value((*it).asCString());
                WritePrivateProfileString(section, key, value, iniFile);
            }
        }

        // --- Shortcuts ---
        if (root.isMember("shortcuts") && root["shortcuts"].isArray()) {
            WritePrivateProfileSection(_T("Shortcuts"), NULL, iniFile); // clear section
            const Json::Value& shortcutsJson = root["shortcuts"];
            for (size_t i = 0; i < shortcutsJson.size(); ++i) {
                const Json::Value& sc = shortcutsJson[i];
                CString key;
                key.Format(_T("%d"), static_cast<int>(i));

                // Compose shortcut string using your ShortcutEncode logic  
                CString shortcutStr;
                if (sc.isString()) {
                    // Directly use the string value
                    shortcutStr = sc.asCString();
                }
                else if (sc.isObject()) {
                    // Compose shortcut string using your ShortcutEncode logic
                    shortcutStr.Format(_T("%s;%s;%s;%s;%d"),
                        sc.get("label", "").asCString(),
                        sc.get("number", "").asCString(),
                        sc.get("type", "").asCString(),
                        sc.get("number2", "").asCString(),
                        sc.get("presence", false).asBool() ? 1 : 0
                    );
                }
                WritePrivateProfileString(_T("Shortcuts"), key, shortcutStr, iniFile);
            }
        }
    }
}
