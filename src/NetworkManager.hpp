#pragma once
#include <Geode/Geode.hpp>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <sstream>

// --- CROSS PLATFORM HEADERS ---
#ifdef GEODE_IS_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET SocketType;
    #define CLOSE_SOCKET closesocket
    #define IS_VALID(s) (s != INVALID_SOCKET)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    typedef int SocketType;
    #define CLOSE_SOCKET close
    #define IS_VALID(s) (s >= 0)
#endif

using namespace geode::prelude;

struct ServerInfo {
    std::string ip;
    std::string name;
};

class NetworkManager {
    SocketType m_tcpSocket = -1;
    SocketType m_udpSocket = -1; 
    
    bool m_running = false;
    std::vector<SocketType> m_clients;
    bool m_isHost = false;

    std::mutex m_discoveryMutex;
    std::map<std::string, ServerInfo> m_discoveredServers;

public:
    static NetworkManager* get() {
        static NetworkManager instance;
        return &instance;
    }

    NetworkManager() {
        #ifdef GEODE_IS_WINDOWS
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        #endif
    }

    // --- HOSTING ---
    void startHost(std::string levelName) {
        if (m_running) return;
        m_isHost = true;
        m_running = true;

        // 1. TCP Server
        std::thread([this]() {
            m_tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
            
            // Allow Port Reuse
            #ifndef GEODE_IS_WINDOWS
            int opt = 1;
            setsockopt(m_tcpSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            #endif

            sockaddr_in serverAddr;
            memset(&serverAddr, 0, sizeof(serverAddr));
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(54321);
            serverAddr.sin_addr.s_addr = INADDR_ANY;

            if (bind(m_tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) return;
            listen(m_tcpSocket, 5);

            while (m_running) {
                sockaddr_in clientAddr;
                #ifdef GEODE_IS_WINDOWS
                int len = sizeof(clientAddr);
                #else
                socklen_t len = sizeof(clientAddr);
                #endif
                
                SocketType client = accept(m_tcpSocket, (sockaddr*)&clientAddr, &len);
                if (IS_VALID(client)) {
                    m_clients.push_back(client);
                    std::thread(&NetworkManager::handleClient, this, client).detach();
                }
            }
        }).detach();

        // 2. UDP Beacon
        std::thread([this, levelName]() {
            SocketType udpSock = socket(AF_INET, SOCK_DGRAM, 0);
            int broadcast = 1;
            setsockopt(udpSock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast));
            
            sockaddr_in broadcastAddr;
            memset(&broadcastAddr, 0, sizeof(broadcastAddr));
            broadcastAddr.sin_family = AF_INET;
            broadcastAddr.sin_port = htons(54322); // Discovery Port
            broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST; // 255.255.255.255

            std::string msg = "GD_LAN:" + levelName;

            while (m_running) {
                sendto(udpSock, msg.c_str(), msg.size(), 0, (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }).detach();
        
        Loader::get()->queueInMainThread([]{
            Notification::create("Hosting LAN Server!", NotificationIcon::Success)->show();
        });
    }

    // --- SEARCHING ---
    void startSearching() {
        m_running = true;
        std::thread([this]() {
            m_udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
            
            sockaddr_in recvAddr;
            memset(&recvAddr, 0, sizeof(recvAddr));
            recvAddr.sin_family = AF_INET;
            recvAddr.sin_port = htons(54322);
            recvAddr.sin_addr.s_addr = INADDR_ANY;
            
            #ifndef GEODE_IS_WINDOWS
            int reuse = 1;
            setsockopt(m_udpSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            #endif

            bind(m_udpSocket, (sockaddr*)&recvAddr, sizeof(recvAddr));

            char buffer[1024];
            while (m_running) {
                sockaddr_in senderAddr;
                #ifdef GEODE_IS_WINDOWS
                int len = sizeof(senderAddr);
                #else
                socklen_t len = sizeof(senderAddr);
                #endif
                
                int n = recvfrom(m_udpSocket, buffer, 1024, 0, (sockaddr*)&senderAddr, &len);
                if (n > 0) {
                    std::string msg(buffer, n);
                    if (msg.find("GD_LAN:") == 0) {
                        std::string ip = inet_ntoa(senderAddr.sin_addr);
                        std::string name = msg.substr(7);
                        
                        std::lock_guard<std::mutex> lock(m_discoveryMutex);
                        m_discoveredServers[ip] = {ip, name};
                    }
                }
            }
        }).detach();
    }

    std::vector<ServerInfo> getFoundServers() {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        std::vector<ServerInfo> list;
        for (auto const& [ip, info] : m_discoveredServers) {
            list.push_back(info);
        }
        return list;
    }

    void connectToServer(std::string ip) {
        m_isHost = false;
        m_running = true;
        std::thread([this, ip]() {
            m_tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(54321);
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

            if (connect(m_tcpSocket, (sockaddr*)&addr, sizeof(addr)) >= 0) {
                Loader::get()->queueInMainThread([]{ Notification::create("Connected!", NotificationIcon::Success)->show(); });
                handleClient(m_tcpSocket);
            }
        }).detach();
    }

    void sendPacket(std::string packet) {
        if (m_isHost) {
            for (auto c : m_clients) send(c, packet.c_str(), packet.size(), 0);
        } else {
            send(m_tcpSocket, packet.c_str(), packet.size(), 0);
        }
    }

    void handleClient(SocketType sock) {
        char buffer[1024];
        while (m_running) {
            int n = recv(sock, buffer, 1024, 0);
            if (n <= 0) break;
            std::string data(buffer, n);
            
            std::stringstream ss(data);
            std::string type;
            std::getline(ss, type, ',');
            
            if (type == "1") { // OBJECT SYNC
                std::string sId, sX, sY;
                std::getline(ss, sId, ','); 
                std::getline(ss, sX, ','); 
                std::getline(ss, sY, ',');
                
                try {
                    int id = std::stoi(sId);
                    float x = std::stof(sX);
                    float y = std::stof(sY);

                    Loader::get()->queueInMainThread([=](){
                        if (auto ed = LevelEditorLayer::get()) {
                            auto obj = ed->createObject(id, ccp(x, y), false);
                            if(obj) obj->setTag(99999);
                        }
                    });
                } catch(...) {}
            }
            
            if (m_isHost) sendPacket(data); 
        }
    }
};