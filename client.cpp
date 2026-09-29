#include <iostream>
#include <vector>
#include <numeric>
#include <cstring>
#include <string>
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
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

int main(int argc, char* argv[]) {
    // Cho phép truyền IP và Port qua tham số dòng lệnh để chạy trên 2 máy khác nhau
    // Cú pháp: ./client [IP_Server] [Port]
    std::string server_ip = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        try {
            port = std::stoi(argv[2]);
        } catch (...) {
            std::cerr << "[CLIENT] Port khong hop le! Su dung port mac dinh: " << DEFAULT_PORT << "\n";
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

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[CLIENT] Loi tao socket!\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "[CLIENT] Dia chi IP khong hop le: " << server_ip << "\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[CLIENT] Dang ket noi toi Server tai " << server_ip << ":" << port << "...\n";
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "[CLIENT] Ket noi toi Server that bai! Vui long kiem tra lai IP/Port va Firewall cua Server.\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    std::cout << "[CLIENT] Da ket noi thanh cong toi Server!\n";

    // 1. Nhận gói tin RES_ARRAY từ Server
    PacketHeader hdr{};
    if (!recv_all(sock, &hdr, sizeof(hdr))) {
        std::cerr << "[CLIENT] Loi nhan Header tu Server!\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (hdr.magic != MAGIC_BYTE || hdr.opcode != Opcode::RES_ARRAY) {
        std::cerr << "[CLIENT] Nhan sai giao thuc hoac Opcode khong khop!\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    uint16_t n = 0;
    if (!recv_all(sock, &n, sizeof(n))) {
        std::cerr << "[CLIENT] Loi nhan so luong phan tu N!\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::vector<int32_t> numbers(n);
    if (!recv_all(sock, numbers.data(), n * sizeof(int32_t))) {
        std::cerr << "[CLIENT] Loi nhan mang so tu Server!\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[CLIENT] Nhan duoc " << n << " so tu Server: ";
    for (int x : numbers) std::cout << x << " ";
    std::cout << "\n";

    // 2. Tính tổng bằng std::accumulate (STL)
    int64_t sum = std::accumulate(numbers.begin(), numbers.end(), 0LL);
    std::cout << "[CLIENT] Tong tinh duoc: " << sum << "\n";

    // 3. Gửi gói tin REQ_SUM lên Server
    PacketHeader req_hdr{MAGIC_BYTE, Opcode::REQ_SUM, sizeof(ReqSumPayload)};
    ReqSumPayload req_payload{sum};

    if (!send_all(sock, &req_hdr, sizeof(req_hdr)) ||
        !send_all(sock, &req_payload, sizeof(req_payload))) {
        std::cerr << "[CLIENT] Loi gui ket qua len Server!\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    std::cout << "[CLIENT] Da gui ket qua tong ve Server.\n";

    // 4. Nhận kết quả đánh giá RES_RESULT từ Server
    PacketHeader res_hdr{};
    ResResultPayload res_payload{};
    if (!recv_all(sock, &res_hdr, sizeof(res_hdr)) ||
        !recv_all(sock, &res_payload, sizeof(res_payload))) {
        std::cerr << "[CLIENT] Loi nhan ket qua danh gia tu Server!\n";
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "\n================ KET QUA DANH GIA ================\n";
    if (res_payload.status == 1) {
        std::cout << "-> KET QUA: DUNG! (Tong chinh xac: " << res_payload.correct_sum << ")\n";
    } else {
        std::cout << "-> KET QUA: SAI! (Tong dung phai la: " << res_payload.correct_sum << ")\n";
    }
    std::cout << "==================================================\n";

    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[CLIENT] Ket thuc chuong trinh.\n";
    return 0;
}
