#include <iostream>
#include <string>
#include <luxon/visualizer.hpp>
#include <luxon/ser_gp_binary_v18.hpp>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <hex_stream>" << std::endl;
        std::cout << "Example: " << argv[0] << " \"00 01 ...\"" << std::endl;
        return 1;
    }

    try {
        luxon::ser::GpBinaryV18 gp_binary_v18;

        // Combine arguments
        std::string input_hex;
        for (int i = 1; i < argc; ++i)
            input_hex += argv[i];

        luxon::ser::ByteArray data = luxon::visualizer::helpers::hex_to_bytes(input_hex);
        std::cout << "Processing " << data.size() << " bytes..." << std::endl;

        if (data.empty()) {
            std::cout << "Error: Input data is empty." << std::endl;
            return 1;
        }

        // Detect raw photon packet
        bool isRawPhoton = (data[0] == 243);

        if (isRawPhoton) {
            std::cout << "Detected raw ser message" << std::endl;
            if (!luxon::visualizer::print_ser_message(data, 0, gp_binary_v18)) {
                std::cout << "Parse failed." << std::endl;
                luxon::visualizer::helpers::print_hex_dump(data, 0);
            }
        } else {
            // Assume UDP/ENet
            luxon::visualizer::print_enet_packet(data, gp_binary_v18);
        }

    } catch (const std::exception& e) {
        std::cerr << "Runtime Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
