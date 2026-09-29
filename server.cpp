#include <iostream>
#include <vector>
#include <random>
#include <numeric>
#include <cstring>
#include <string>
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #define CLOSE_SOCKET(s) close(s)
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#define DEFAULT_PORT 8080
#define MAGIC_BYTE 0xAA

enum class Opcode : uint8_t {
    RES_ARRAY  = 0x01,
    REQ_SUM    = 0x02,
    RES_RESULT = 0x03,
    ERR_MSG    = 0xFF
};

#pragma pack(push, 1)
struct PacketHeader {
    uint8_t  magic;     // 0xAA
    Opcode   opcode;    // Loại gói tin
    uint16_t length;    // Độ dài payload theo sau
};

struct ReqSumPayload {
    int64_t client_sum;
};

struct ResResultPayload {
    uint8_t status;      // 1 = DUNG, 0 = SAI
    int64_t correct_sum;
};
#pragma pack(pop)

bool send_all(SOCKET sock, const void* data, size_t size) {
    const char* ptr = static_cast<const char*>(data);
    while (size > 0) {
        int sent = send(sock, ptr, static_cast<int>(size), 0);
        if (sent <= 0) return false;
        ptr += sent;
        size -= sent;
    }
    return true;
}

bool recv_all(SOCKET sock, void* data, size_t size) {
    char* ptr = static_cast<char*>(data);
    while (size > 0) {
        int received = recv(sock, ptr, static_cast<int>(size), 0);
        if (received <= 0) return false;
        ptr += received;
        size -= received;
    }
    return true;
}

void handle_client(SOCKET client_sock) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int32_t> dis(1, 100);

    uint16_t n = 5; // Số lượng phần tử N
    std::vector<int32_t> numbers(n);
    for (auto& x : numbers) {
        x = dis(gen);
    }

    int64_t expected_sum = std::accumulate(numbers.begin(), numbers.end(), 0LL);

    std::cout << "[SERVER] Sinh danh sach " << n << " so: ";
    for (int x : numbers) std::cout << x << " ";
    std::cout << "\n[SERVER] Tong thuc te can tinh: " << expected_sum << "\n";

    // 2. Gửi gói tin RES_ARRAY cho Client
    uint16_t payload_len = static_cast<uint16_t>(sizeof(uint16_t) + numbers.size() * sizeof(int32_t));
    PacketHeader hdr{MAGIC_BYTE, Opcode::RES_ARRAY, payload_len};

    if (!send_all(client_sock, &hdr, sizeof(hdr)) ||
        !send_all(client_sock, &n, sizeof(n)) ||
        !send_all(client_sock, numbers.data(), numbers.size() * sizeof(int32_t))) {
        std::cerr << "[SERVER] Loi khi gui RES_ARRAY cho Client!\n";
        return;
    }
    std::cout << "[SERVER] Da gui mang so sang Client.\n";

    // 3. Nhận kết quả REQ_SUM từ Client
    PacketHeader client_hdr{};
    if (!recv_all(client_sock, &client_hdr, sizeof(client_hdr))) {
        std::cerr << "[SERVER] Loi khi nhan Header tu Client!\n";
        return;
    }

    if (client_hdr.magic != MAGIC_BYTE || client_hdr.opcode != Opcode::REQ_SUM) {
        std::cerr << "[SERVER] Goi tin khong hop le hoac sai Opcode!\n";
        return;
    }

    ReqSumPayload req_payload{};
    if (!recv_all(client_sock, &req_payload, sizeof(req_payload))) {
        std::cerr << "[SERVER] Loi khi doc Payload tu Client!\n";
        return;
    }
    std::cout << "[SERVER] Nhan ket qua tong tu Client: " << req_payload.client_sum << "\n";

    // 4. Kiểm tra và trả về gói RES_RESULT
    ResResultPayload res_payload{};
    res_payload.status = (req_payload.client_sum == expected_sum) ? 1 : 0;
    res_payload.correct_sum = expected_sum;

    PacketHeader res_hdr{MAGIC_BYTE, Opcode::RES_RESULT, sizeof(ResResultPayload)};
    send_all(client_sock, &res_hdr, sizeof(res_hdr));
    send_all(client_sock, &res_payload, sizeof(res_payload));

    std::cout << "[SERVER] Danh gia: " 
              << (res_payload.status == 1 ? ">> DUNG <<" : ">> SAI <<") << "\n";
}

int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "[SERVER] Port khong hop le! Su dung port mac dinh: " << DEFAULT_PORT << "\n";
            port = DEFAULT_PORT;
        }
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup that bai!\n";
        return 1;
    }
#endif

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "Loi tao socket!\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Chấp nhận kết nối từ MỌI IP/card mạng (LAN, Wi-Fi, Internet)
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Loi bind socket vao port " << port << "!\n";
        CLOSE_SOCKET(server_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server_fd, 5) == SOCKET_ERROR) {
        std::cerr << "Loi listen!\n";
        CLOSE_SOCKET(server_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "====================================================\n";
    std::cout << "  SERVER DANG LANG NGHE TAI CONG " << port << "\n";
    std::cout << "  (Cho phep ket noi tu localhost, LAN va Internet)\n";
    std::cout << "====================================================\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        SOCKET client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);

        if (client_sock == INVALID_SOCKET) {
            std::cerr << "Loi accept!\n";
            continue;
        }

        std::cout << "\n[SERVER] Client da ket noi: " 
                  << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << "\n";

        handle_client(client_sock);
        CLOSE_SOCKET(client_sock);
        std::cout << "[SERVER] Da dong phien lam viec voi Client.\n";
    }

    CLOSE_SOCKET(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
