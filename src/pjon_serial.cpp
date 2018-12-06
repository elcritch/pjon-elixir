#include "pjon_serial.h"


#include <PJON.h>

#include "erl_comm.hpp"

uint8_t back_off_degree = TS_BACK_OFF_DEGREE;
uint8_t max_attempts = TS_MAX_ATTEMPTS;
uint16_t response_time_out = RESPONSE_TIME_OUT;
uint16_t byte_time_out = BYTE_TIME_OUT;

// struct FirmwareThroughSerial : public ThroughSerial {

//   uint32_t back_off(uint8_t attempts) {
//     uint32_t result = attempts;
//     for(uint8_t d = 0; d < back_off_degree; d++)
//       result *= (uint32_t)(attempts);
//     return result;
//   };

//   uint16_t receive_byte() {
//     return ThroughSerial::receive_byte(byte_time_out);
//   }

//   uint16_t receive_byte(uint32_t time_out) {
//     return ThroughSerial::receive_byte(time_out);
//   }

//   static uint8_t get_max_attempts() {
//     return max_attempts;
//   };

//   uint16_t receive_response() {
//     return receive_byte(response_time_out);
//   };
// };


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

#define LOGFILE DEBUG_LOGFILE

int main(int argc, char const *argv[]) {
  const char *device = argv[1];
  int baud_rate = std::stoi(argv[2]);

  #ifdef DEBUG_MODE // useful for debugging
    std::ofstream out(DEBUG_LOGFILE);
    std::streambuf *coutbuf = std::cout.rdbuf(); //save old buf
    std::cerr.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!
  #endif

  std::cerr << "Setting serial... file: " << device << std::endl;
  std::cerr << "Setting serial... baud: " << baud_rate << std::endl;

  int s = serialOpen(device, baud_rate);
  if(int(s) < 0) {
    std::cerr << "Serial open fail!" << std::endl;
    exit(1);
  }

  PJON<ThroughSerialAsync> bus(BUS_ADDR);

  bus.strategy.set_serial(s);
  bus.set_packet_id(true);
  // bus.set_synchronous_acknowledge(false);
  // bus.set_asynchronous_acknowledge(true);


#ifdef RPI // set for rpi
  bus.strategy.set_baud_rate(baud_rate );
#endif

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

        #if DEBUG_VERBOSE > 0
          std::cerr << "erl_comms reading... " << std::endl;
        #endif // DEBUG_VERBOSE

        pk_len_t cmd_sz =
          read_port_cmd<pk_len_t>(port_rx_buffer, PJON_PACKET_MAX_LENGTH);
        port_rx_len = cmd_sz;

        #if DEBUG_VERBOSE > 0
          std::cerr << " erl_comms read: " << cmd_sz << std::endl;
        #endif // DEBUG_VERBOSE

        if (cmd_sz == 0) {
          std::cerr << "STDIN closed, exiting. " << std::endl;
          exit(0);
        }
      }
      usleep(SERIAL_FREAD_LOOP_DELAY);
    }
  }).detach();

  do {
    bus.receive(PJON_RX_WAIT_TIME);
    bus.update();

    if (port_rx_len.load() > 0) {
      int rx_len = port_rx_len.load();

      long start_time = micros();

      #if DEBUG_VERBOSE > 0
        std::cerr << " blocking " #PJON_SEND_TYPE " ";
      #endif // DEBUG_VERBOSE
        int resp = bus.PJON_SEND_TYPE(TX_PACKET_ADDR, port_rx_buffer, rx_len );

      #if DEBUG_VERBOSE > 0
        long end_time = micros();
        std::cerr << " pjon packet wrote: "
                  << " time: " << (end_time - start_time)
                  << " size: " << rx_len
                  << " response: " << resp
                  << std::endl;
      #endif // DEBUG_VERBOSE

      port_rx_len = 0;
    }
    usleep(SERIAL_SREAD_LOOP_DELAY);
  } while (true);

  std::cerr << "exiting..." << std::endl;
  exit(0);
};

