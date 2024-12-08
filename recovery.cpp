
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <bits/stdc++.h>

#include "recovery.hpp"
#include "betree.hpp"
#include "debug.hpp"
Recovery::Recovery(swap_space *sspace_ptr, betree<uint64_t, std::string> *tree) : sspace_ptr(sspace_ptr), tree(tree)
{
}

void Recovery::do_recovery()
{

    // if (!logger->log_exists()) {
    // std::cout << "No log file found. starting with an empty tree." << std::endl;
    // return;
    // }
    std::cout << "Starting Recovery...\n";
    
    sspace_ptr->parse_master_log();

    sspace_ptr->rebuild_tree();

    uint64_t last_checkpoint_lsn = read_master_record();

    replay_log(last_checkpoint_lsn);

    std::cout << "Recovery completed successfully" << std::endl;
}

uint64_t Recovery::read_master_record()
{

    std::ifstream master_record("master_record.txt");

    if (!master_record.is_open())
    {
        std::cerr << "Assuming no checkpoint exists.\n";
        return 0;
    }

    uint64_t lsn = 0;
    master_record >> lsn;
    master_record.close();
    return lsn;
}

void Recovery::replay_log(uint64_t last_checkpoint_lsn)
{
    std::cout << "Replaying Logs...\n";

    std::ifstream log_stream("wal_log.txt");
    if (!log_stream.is_open())
    {
        std::cerr << "Couldn't open WAL log file for recovery.\n";
        return;
    }

    std::string line;

    std::cout << "Replying LSN " << last_checkpoint_lsn << std::endl;

    while (std::getline(log_stream, line))
    {
        std::istringstream iss(line);
        uint64_t lsn;
        iss >> lsn;

        if (lsn <= last_checkpoint_lsn)
            continue;

        std::string operation;
        int key;
        std::string value;

        iss >> operation >> key;

        iss >> value;
        value.erase(std::remove(value.begin(), value.end(), ':'), value.end());

        debug(std::cout << "OPERATION" << operation << "key " << key << "value " << value << std::endl);
        if (operation == "INSERT")
        {
            debug(std::cout << "Replying Insert " << key << " value " << value << std::endl);
            tree->insert(key, value);
        }
        else if (operation == "DELETE")
        {
            debug(std::cout << "Replying Delete " << key << std::endl);
            tree->erase(key);
        }
        else if (operation == "UPDATE")
        {
            debug(std::cout << "Replying Update " << key << " value " << value << std::endl);
            tree->update(key, value);
        }
    }

    log_stream.close();
}
