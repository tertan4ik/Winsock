#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 512

SOCKET ConnectSocket = INVALID_SOCKET; // Сокет для подключения к серверу
char recvbuf[DEFAULT_BUFLEN]; // Буфер для получения данных
bool running = true; // Флаг для управления потоком

// Функция для получения сообщений от сервера в отдельном потоке
DWORD WINAPI ReceiveMessages(LPVOID lpParam) {
    while (running) {
        int iResult = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN, 0); // Получение данных
        if (iResult > 0) {
            recvbuf[iResult] = '\0';
            std::cout << recvbuf; // Вывод сообщения
        }
        else {
            running = false;
            break;
        }
    }
    return 0;
}

int main() {
    WSADATA wsaData;
    struct addrinfo* result = NULL, * ptr = NULL, hints;
    std::string serverIp; // Переменная для хранения IP-адреса сервера
    std::string username;

    // Инициализация Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    // Цикл для повторного запроса IP-адреса при ошибке
    while (true) {
        // Запрос IP-адреса сервера у пользователя
        std::cout << "Enter server IP address: ";
        std::getline(std::cin, serverIp);

        // Настройка параметров для getaddrinfo
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC; // IPv4 или IPv6
        hints.ai_socktype = SOCK_STREAM; // Потоковый сокет (TCP)
        hints.ai_protocol = IPPROTO_TCP; // Протокол TCP

        // Получение информации о сервере
        if (getaddrinfo(serverIp.c_str(), DEFAULT_PORT, &hints, &result) != 0) {
            std::cerr << "Invalid IP address or hostname. Please try again.\n";
            continue; // Повторный запрос IP-адреса
        }

        // Попытка подключения к серверу
        for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
            ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol); // Создание сокета
            if (ConnectSocket == INVALID_SOCKET) {
                std::cerr << "Socket creation failed.\n";
                WSACleanup();
                return 1;
            }

            // Подключение к серверу
            if (connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
                closesocket(ConnectSocket);
                ConnectSocket = INVALID_SOCKET;
                continue;
            }
            break;
        }

        freeaddrinfo(result); // Освобождение памяти

        if (ConnectSocket == INVALID_SOCKET) {
            std::cerr << "Unable to connect to server. Please check the IP address and try again.\n";
            continue; // Повторный запрос IP-адреса
        }

        break; // Выход из цикла при успешном подключении
    }

    // Запрос имени пользователя
    std::cout << "Enter your username: ";
    std::getline(std::cin, username);
    send(ConnectSocket, username.c_str(), username.length(), 0); // Отправка имени пользователя

    // Создание потока для получения сообщений
    HANDLE hThread = CreateThread(NULL, 0, ReceiveMessages, NULL, 0, NULL);
    if (!hThread) {
        std::cerr << "Failed to create receive thread.\n";
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    // Основной цикл для отправки сообщений
    while (running) {
        std::string message;
        std::getline(std::cin, message);
        if (message == "/exit") {
            running = false;
            break;
        }
        send(ConnectSocket, message.c_str(), message.length(), 0); // Отправка сообщения
    }

    // Закрытие сокета и очистка Winsock
    closesocket(ConnectSocket);
    WSACleanup();
    return 0;
}