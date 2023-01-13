//
// Created by thegreatleapforward on 03/01/23.
//
#include <dpp/dpp.h>
#include <spdlog/fmt/fmt.h>
#include <ranges>
#include <iostream>
#include "guides.h"
#include "commands.h"
#include "backend.h"

namespace ran = std::ranges;

int main() {
    const std::string filename = "token.txt";
    auto token = std::ifstream{filename};
    std::string BOT_TOKEN;
    if (!token.is_open()) {
        std::cout << "failed to open " << filename << "\n";
        exit(1);
    }
    else {
        token >> BOT_TOKEN;
    }

    dpp::cluster bot(BOT_TOKEN,
                     dpp::i_message_content | dpp::i_default_intents | dpp::i_guild_members);

    bot.on_log(dpp::utility::cout_logger());

    std::unordered_map<snowflake, guild_state> states;
    std::shared_mutex states_mut_mutex;

    id_vec guild_ids {820855382472785921};
    id_vec dojo_ids {820855382472785921};

    std::promise<void> exec_stop;
    auto fut = exec_stop.get_future();

    bot.on_guild_create([&states, &states_mut_mutex, &bot, &guild_ids, &dojo_ids, &exec_stop]
    (const dpp::guild_create_t& event){
        const auto id = event.created->id;
        if (ran::find(guild_ids, id) == guild_ids.cend()) {
            bot.current_user_leave_guild(id);
        }
        else {
            {
                auto lock = std::unique_lock(states_mut_mutex);
                states.emplace(std::piecewise_construct,
                               std::forward_as_tuple(id), std::forward_as_tuple(bot, exec_stop, event.created));
            }
            auto& state = states.at(id);

            if (ran::find(dojo_ids, id) != dojo_ids.end()) {
                state.dojo_info.emplace(&bot, id);
                state.command_handler.dojo_cmds();
            }
            state.command_handler.default_cmds().register_cmds();
        }
    });

    bot.on_channel_delete([&states](const dpp::channel_delete_t& event){
        category_clear(event.deleted->id,states.at(event.deleting_guild->id));
    });

    bot.on_guild_member_add([&states](const dpp::guild_member_add_t& event){
        auto msg = message{fmt::format("Welcome to the server, {}", event.added.get_mention())};
        states.at(event.adding_guild->id).welcomer.send(msg);
    });

    bot.on_guild_member_remove([&states](const dpp::guild_member_remove_t& event){
        user_category_clear(event.removed->id,states.at(event.removing_guild->id));
        states.at(event.removed->id).msg_cache.mt_remove_user(event.removed->id);
    });

    //probably don't need to actually cache the messages, just keep a running total
    bot.on_message_create([&states](const dpp::message_create_t& event){
        states.at(event.msg.guild_id).msg_cache.mt_insert(event.msg);
    });

    bot.on_message_update([&states](const dpp::message_update_t& event){
        states.at(event.msg.guild_id).msg_cache.mt_insert(event.msg), true;
    });

    bot.on_slashcommand([&states](const slashcommand_t& event) {
        states.at(event.command.guild_id).command_handler.handle<slashcommand_t>(event);
    });
    bot.on_user_context_menu([&states](const user_context_menu_t& event){
        states.at(event.command.guild_id).command_handler.handle<user_context_menu_t>(event);
    });
    bot.on_message_context_menu([&states](const message_context_menu_t& event){
        states.at(event.command.guild_id).command_handler.handle<message_context_menu_t>(event);
    });

    bot.on_ready([&bot](const dpp::ready_t& event){

    });

    bot.start(dpp::st_return);
    fut.wait();
}
