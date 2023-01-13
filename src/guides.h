//
// Created by thegreatleapforward on 10/01/23.
//


#pragma once

#include <vector>
#include <dpp/dpp.h>

using coc_vec = std::vector<dpp::command_option_choice>;

struct guide {};

constexpr guide g;

inline coc_vec guilds_options() { return {}; }
