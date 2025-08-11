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
