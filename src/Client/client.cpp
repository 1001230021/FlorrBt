#include "../Shared/game_config.h"
#include "../Shared/network_msg.h"
#include <SFML/Network/IpAddress.hpp>
#include <SFML/Network/TcpSocket.hpp>
#include <SFML/System/Time.hpp>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

int main()
{
    sf::TcpSocket socket;

    sf::Socket::Status status = socket.connect(sf::IpAddress::LocalHost, game_config::port, sf::seconds(5));
    if (status != sf::Socket::Status::Done)
    {
        std::cerr << "Failed to connect to server\n";
        return 1;
    }
    std::cout << "Connected to server\n";

    uint8_t buffer[3] = {};
    ClientOperate move_op;
    move_op.type = ClientOperate::Type::Input;
    move_op.move_x = 127;
    move_op.move_y = 0;

    size_t len = ClientOperate::pack(move_op, buffer);
    if (socket.send(buffer, len) != sf::Socket::Status::Done)
    {
        std::cerr << "Failed to send movement\n";
        return 1;
    }
    std::cout << "Sent movement\n";

    ClientOperate attack_op;
    attack_op.type = ClientOperate::Type::Chores;
    attack_op.is_attacking = true;
    attack_op.is_defending = false;

    len = ClientOperate::pack(attack_op, buffer);
    if (socket.send(buffer, len) != sf::Socket::Status::Done)
    {
        std::cerr << "Failed to send attack\n";
        return 1;
    }
    std::cout << "Sent attack\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    socket.disconnect();
    std::cout << "Disconnected\n";
    return 0;
}
