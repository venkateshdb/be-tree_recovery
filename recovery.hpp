#ifndef RECOVERY_HPP
#define RECOVERY_HPP

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include "swap_space.hpp" 

template <class Key, class Value>
class betree;

class Recovery {
public:
    Recovery(swap_space* sspace_ptr, betree<uint64_t, std::string>* tree);
    void do_recovery();

private:
    swap_space* sspace_ptr;
    betree<uint64_t, std::string>* tree;
    uint64_t read_master_record();
    void replay_log(uint64_t last_checkpoint_lsn);
};

#endif 




