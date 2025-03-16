#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "27015"
#define DEFAULT_BUFLEN 512
#define MAX_CLIENTS 256

CRITICAL_SECTION cs; // Критическая секция для синхронизации потоков
std::vector<SOCKET> clients; // Вектор для хранения сокетов клиентов
std::vector<std::string> users; // Вектор для хранения имен пользователей

// Функция для рассылки сообщений всем клиентам, кроме отправителя
void BroadcastMessage(const std::string& message, SOCKET sender = INVALID_SOCKET) {
    EnterCriticalSection(&cs); // Вход в критическую секцию
    for (SOCKET client : clients) {
        if (client != sender) {
            send(client, message.c_str(), message.length(), 0); // Отправка сообщения клиенту
        }
    }
    std::cout << message; // Вывод сообщения в консоль сервера
    LeaveCriticalSection(&cs); // Выход из критической секции
}

// Функция для обработки клиента в отдельном потоке
DWORD WINAPI ClientHandler(LPVOID clientSocket) {
    SOCKET ClientSocket = *(SOCKET*)clientSocket;
    char recvbuf[DEFAULT_BUFLEN];
    int iResult;

    std::string username;

    // Получение имени пользователя
    iResult = recv(ClientSocket, recvbuf, DEFAULT_BUFLEN, 0);
    if (iResult > 0) {
        recvbuf[iResult] = '\0';
        username = recvbuf;

        EnterCriticalSection(&cs);
        users.push_back(username); // Добавление пользователя в список
        LeaveCriticalSection(&cs);

        std::string joinMsg = "[SERVER]: user \"" + username + "\" has joined\n";
        BroadcastMessage(joinMsg); // Рассылка сообщения о подключении
    }
    else {
        closesocket(ClientSocket);
        return 0;
    }

    // Обработка сообщений от клиента
    while (true) {
        iResult = recv(ClientSocket, recvbuf, DEFAULT_BUFLEN, 0);
        if (iResult > 0) {
            recvbuf[iResult] = '\0';
            std::string message = recvbuf;

            if (message == "/users") {
                std::string userList = "[SERVER]: Active users:\n";
                EnterCriticalSection(&cs);
                for (const auto& user : users) {
                    userList += "- " + user + "\n";
                }
                LeaveCriticalSection(&cs);
                send(ClientSocket, userList.c_str(), userList.length(), 0); // Отправка списка пользователей
            }
            else if (message == "/exit") {
                break;
            }
            else {
                std::string formattedMessage = "[" + username + "]: " + message + "\n";
                BroadcastMessage(formattedMessage, ClientSocket); // Рассылка сообщения всем клиентам
            }
        }
        else {
            break;
        }
    }

    // Удаление клиента из списка
    EnterCriticalSection(&cs);
    clients.erase(std::remove(clients.begin(), clients.end(), ClientSocket), clients.end());
    users.erase(std::remove(users.begin(), users.end(), username), users.end());
    LeaveCriticalSection(&cs);

    std::string leaveMsg = "[SERVER]: user \"" + username + "\" left the chat\n";
    BroadcastMessage(leaveMsg); // Рассылка сообщения о выходе пользователя

    closesocket(ClientSocket);
    return 0;
}

// Функция для получения IPv4-адреса активного интерфейса
std::string GetLocalIPv4() {
    char host[NI_MAXHOST];
    if (gethostname(host, NI_MAXHOST) != 0) {
        return "0.0.0.0";
    }

    struct addrinfo hints, * res;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET; // Только IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        return "0.0.0.0";
    }

    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in*)res->ai_addr)->sin_addr, ipstr, INET_ADDRSTRLEN);

    freeaddrinfo(res);
    return std::string(ipstr);
}

int main() {
    WSADATA wsaData;
    struct addrinfo* result = NULL, hints;
    SOCKET ListenSocket = INVALID_SOCKET;

    InitializeCriticalSection(&cs); // Инициализация критической секции
    WSAStartup(MAKEWORD(2, 2), &wsaData); // Инициализация Winsock

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET; // Используем только IPv4
    hints.ai_socktype = SOCK_STREAM; // Потоковый сокет (TCP)
    hints.ai_protocol = IPPROTO_TCP; // Протокол TCP
    hints.ai_flags = AI_PASSIVE; // Сервер будет слушать на всех интерфейсах

    // Получение информации о адресе
    if (getaddrinfo(NULL, DEFAULT_PORT, &hints, &result) != 0) {
        std::cerr << "getaddrinfo failed.\n";
        WSACleanup();
        return 1;
    }

    // Создание сокета
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Привязка сокета к адресу
    if (bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        closesocket(ListenSocket);
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Начало прослушивания
    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed.\n";
        closesocket(ListenSocket);
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Получение и вывод IPv4-адреса сервера
    std::string localIP = GetLocalIPv4();
    std::cout << "Server started on IPv4: " << localIP << ", Port: " << DEFAULT_PORT << std::endl;

    freeaddrinfo(result); // Освобождение памяти

    while (true) {
        SOCKET ClientSocket = accept(ListenSocket, NULL, NULL); // Принятие подключения
        if (ClientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
            continue;
        }

        EnterCriticalSection(&cs);
        if (clients.size() < MAX_CLIENTS) {
            clients.push_back(ClientSocket); // Добавление клиента в список
            LeaveCriticalSection(&cs);

            CreateThread(NULL, 0, ClientHandler, &ClientSocket, 0, NULL); // Создание потока для клиента
        }
        else {
            LeaveCriticalSection(&cs);
            std::string msg = "[SERVER]: Chat is full. Try again later.\n";
            send(ClientSocket, msg.c_str(), msg.length(), 0); // Отправка сообщения о переполнении
            closesocket(ClientSocket);
        }
    }

    DeleteCriticalSection(&cs); // Удаление критической секции
    closesocket(ListenSocket); // Закрытие сокета
    WSACleanup(); // Очистка Winsock
    return 0;
}