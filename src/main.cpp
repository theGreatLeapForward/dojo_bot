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
        auto lock = std::unique_lock(states_mut_mutex);

        const auto id = event.created->id;
        if (ran::find(guild_ids, id) == guild_ids.cend()) {
            bot.current_user_leave_guild(id);
        }
        else {
            if (dpp::run_once<struct guild_info_init>()) {

                guild_state state;

                auto state = guild_state {
                    .bot = bot, .exec_stop = exec_stop, .guild_ids = guild_ids,
                    .guild_id = id, .msg_cache = guild_user_msg_cache{event.created, &bot}
                };

                //Start message cache
                ref.msg_cache.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(id), std::forward_as_tuple(event.created, &ref.bot));

                //Add the new command_handlers
                ref.command_handlers.emplace(std::piecewise_construct,std::forward_as_tuple(id), std::forward_as_tuple(&ref.bot, id, ref));

                //Is the guild the dojo? We need to cache its special info as well
                if (ran::find(ref.dojo_ids, id) != ref.dojo_ids.end()) {
                    ref.dojo_infos.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(id), std::forward_as_tuple(&ref.bot, id));

                    ref.command_handlers.at(id).dojo_cmds();
                }

                ref.command_handlers.at(id).default_cmds().register_cmds();
            }
        }
    });

    bot.on_channel_delete([&states](const dpp::channel_delete_t& event){
        auto& info = states.at(event.deleting_guild->id).dojo_info;
        if (info.has_value()) {
            info->mt_check_channel(*event.deleted, true);
        }
    });

    bot.on_guild_member_add([&states](const dpp::guild_member_add_t& event){

    });

    bot.on_guild_member_remove([&dojo_infos](const dpp::guild_member_remove_t& event){

    });

    //probably don't need to actually cache the messages, just keep a running total
    bot.on_message_create([&msg_cache](const dpp::message_create_t& event){
        msg_cache.at(event.msg.guild_id).mt_insert(event.msg);
    });

    bot.on_message_update([](const dpp::message_update_t& event){

    });

    bot.on_slashcommand([&command_handlers](const slashcommand_t& event) {
        command_handlers.at(event.command.guild_id).handle<slashcommand_t>(event);
    });
    bot.on_user_context_menu([&command_handlers](const user_context_menu_t& event){
        command_handlers.at(event.command.guild_id).handle<user_context_menu_t>(event);
    });
    bot.on_message_context_menu([&command_handlers](const message_context_menu_t& event){
        command_handlers.at(event.command.guild_id).handle<message_context_menu_t>(event);
    });

    bot.start(dpp::st_return);
    fut.wait();
}
