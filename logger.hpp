#ifndef LOGGER_HPP
#define LOGGER_HPP
#include <fstream>
#include <iostream>
#include <string>
#include <cassert>
#include "backing_store.hpp"
#include "swap_space.hpp"

class Logger {
public:
    Logger(backing_store* storage, uint64_t persistence_granularity, uint64_t checkpoint_granularity)
        : storage(storage),
          persistence_granularity(persistence_granularity), 
          log_count(0), 
          lsn(0),
          checkpoint_granularity(checkpoint_granularity),
          operations_after_last_checkpoint(0) {

        log_stream.open("wal_log.txt", std::ios::out | std::ios::app);  // file handling
        
        if (!log_stream.is_open()) {
            std::cerr << "Failed to open log file for WAL" << std::endl;
            exit(1);
        }

    }

    ~Logger() {
        if (log_stream.is_open()) {
            log_stream.close();
        }
    }

    void log_operation(int opcode, uint64_t key, const std::string& value){
        lsn ++;

        if (!log_stream.is_open()){
            std::cerr << "Logging file is not open!" << std::endl;
            return;
        }
        
        // Logging operation type
        log_stream << lsn << " ";
        switch (opcode) {
            case 0: log_stream << "INSERT "; break;
            case 1: log_stream << "DELETE "; break;
            case 2: log_stream << "UPDATE "; break;
        }

        log_stream << key;

        if (!value.empty()) {
            log_stream << " " << value;
        }

        log_stream << std::endl;

        log_count++;
        operations_after_last_checkpoint++;

        // Persist if log count reaches persistence granularity
        if (log_count >= persistence_granularity) {
            persist(); 
        }
    }

    bool log_exists() const {
        std::ifstream log_file("wal_log.txt");
        return log_file.good();
    }

    bool need_checkpoint() const {
        debug(std::cout << "CHECKPOINT" << operations_after_last_checkpoint << std::endl);
        return operations_after_last_checkpoint >= checkpoint_granularity;
    }

    uint64_t get_current_lsn() const {
        return lsn;
    }

    void checkpoint(uint64_t lsn) {
        // log_stream << "Checkpoint at" << lsn << std::endl;
        log_stream.flush();

        // // Update master record
        // ss->update_master_record();

        // Truncate log file
        if (log_stream.is_open()){
            log_stream.close(); // close to change mode
        }

        log_stream.open("wal_log.txt", std::ios::out | std::ios::trunc);
        if (!log_stream.is_open()) {
            std::cerr << "Failed to open wal_log.txt" << std::endl;
            exit(1);
        }
        // Reset the operation after checkpoint
        operations_after_last_checkpoint = 0;
    }


private:
    // Flush the log file to disk
    void persist() {
        std::cerr << "Persisting log to disk..." << std::endl;
        if (!log_stream.is_open() || log_stream.bad()) {
            std::cerr << "Cannot persist" << std::endl;
            return;
        }
        log_stream.flush();
        log_count = 0;  // Reset count after persisting
        std::cerr << "Persisted successfully" << std::endl;
    }

    backing_store* storage;
    std::ofstream log_stream;
    uint64_t persistence_granularity;
    uint64_t log_count;
    uint64_t lsn;
    uint64_t checkpoint_granularity;
    uint64_t operations_after_last_checkpoint;
};

#endif // LOGGER_HPP
