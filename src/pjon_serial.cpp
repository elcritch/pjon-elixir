#include "pjon_serial.h"


#include <PJON.h>

#include "erl_comm.hpp"

std::atomic<size_t> port_rx_len;
char port_rx_buffer[BUFFER_SIZE];

void receiver_function(uint8_t *payload,
                       uint16_t length,
                       const PJON_Packet_Info &packet_info)
{
  write_port_cmd<pk_len_t>( (char*)payload, length);
}

void error_handler(uint8_t code,
                   uint16_t data,
                   void *custom_pointer)
{
  if (code == PJON_CONNECTION_LOST) {
    std::cerr << "error: pjon connection lost" << std::endl;
  }
  else {
    std::cerr << "error: pjon packet failure" << std::endl;
  }
}

// #define _STRINGIFY(X) #X
// #define STRINGIFY(X) _STRINGIFY2(X)

#define LOGFILE DEBUG_LOGFILE

int main(int argc, char const *argv[]) {
  const char *device = argv[1];
  int baud_rate = std::stoi(argv[2]);

  #ifdef DEBUG_MODE // useful for debugging
    std::ofstream out(LOGFILE);
    std::streambuf *coutbuf = std::cout.rdbuf(); //save old buf
    std::cerr.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!
  #endif

  std::cerr << "Setting serial... file: " << device << std::endl;
  std::cerr << "Setting serial... baud: " << baud_rate << std::endl;

  PJON<PJON_STRATEGY> bus(BUS_ADDR);

  // bus.strategy.set_serial(&serial);
  int s = serialOpen(device, baud_rate);
  if(int(s) < 0) {
    std::cerr << "Serial open fail!" << std::endl;
    exit(1);
  }
  bus.strategy.set_serial(s);

  // bus.strategy.set_baud_rate(baud_rate);
  bus.set_receiver(receiver_function);
  bus.set_error(error_handler);

  std::cerr << "Opening bus" << std::endl;

  bus.begin();
  std::cerr << "Success, starting communication" << std::endl;

  // Thread to handle reading input port commands
  std::thread([&]{
    port_rx_len = 0;
    while (true) {
      if (port_rx_len.load() == 0) {
        // std::cerr << "port port_command reading... " << std::endl;
        pk_len_t cmd_sz =
          read_port_cmd<pk_len_t>(port_rx_buffer, PJON_PACKET_MAX_LENGTH);
        port_rx_len = cmd_sz;
        if (cmd_sz == 0) {
          std::cerr << "STDIN closed, exiting. " << std::endl;
          exit(0);
        }
      }
      usleep(100);
    }
  }).detach();

  do {
    bus.receive(PJON_RX_WAIT_TIME);
    bus.update();

    if (port_rx_len.load() > 0) {
      int rx_len = port_rx_len.load();

      #if PJON_SEND_BLOCKING == true
        int resp = bus.send_packet_blocking(TX_PACKET_ADDR, port_rx_buffer, rx_len );
      #else
        int resp = bus.send_packet(TX_PACKET_ADDR, port_rx_buffer, rx_len);
      #endif

      port_rx_len = 0;
    }
  } while (true);

  std::cerr << "exiting..." << std::endl;
  exit(0);
};

