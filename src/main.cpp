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
    {
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
    bot.me.id = 988671935577718784;

    dpp::cache<guild_state> guild_states;

    id_vec guild_ids {820855382472785921};
    id_vec dojo_ids {820855382472785921};

    std::promise<void> exec_stop;
    auto fut = exec_stop.get_future();

    bot.on_guild_create([&guild_states, &bot, &guild_ids, &dojo_ids, &exec_stop]
    (const dpp::guild_create_t& event){

        const auto id = event.created->id;
        if (ran::find(guild_ids, id) == guild_ids.cend()) {
            bot.current_user_leave_guild(id);
            bot.log(dpp::ll_info, fmt::format("Bot was added to unknown guild {}. "
                                              "This will most likely cause multiple read access exceptions.", id));
        }
        else {
            {
                auto* ptr = new guild_state{bot, exec_stop, event.created};
                guild_states.store(ptr);
            }
            auto& state = *guild_states.find(id);

            if (ran::find(dojo_ids, id) != dojo_ids.end()) {
                state.dojo_info.emplace(&bot, id);
                state.cmd_handler.dojo_cmds();
            }
            state.cmd_handler.default_cmds().create();
        }
    });

    bot.on_guild_delete([&guild_states](const auto& event){
        guild_states.remove(guild_states.find(event.deleted->id));
    });

    bot.on_channel_delete([&guild_states](const dpp::channel_delete_t& event){
        category_clear(event.deleted->id,*guild_states.find(event.deleting_guild->id));
    });

    bot.on_guild_member_add([&guild_states](const dpp::guild_member_add_t& event){
        auto msg = message{fmt::format("Welcome to the server, {}", event.added.get_mention())};
        guild_states.find(event.adding_guild->id)->welcomer.send(msg);
    });

    bot.on_guild_member_remove([&guild_states](const dpp::guild_member_remove_t& event){
        user_category_clear(event.removed->id,*guild_states.find(event.removing_guild->id));
        guild_states.find(event.removed->id)->msg_cache.mt_remove_user(event.removed->id);
    });

    //probably don't need to actually cache the messages, just keep a running total
    bot.on_message_create([&guild_states](const dpp::message_create_t& event){
        guild_states.find(event.msg.guild_id)->msg_cache.mt_insert(event.msg);
    });

    bot.on_message_update([&guild_states](const dpp::message_update_t& event){
        guild_states.find(event.msg.guild_id)->msg_cache.mt_insert(event.msg);
    });

    bot.on_slashcommand([&guild_states](const slashcommand_t& event) {
        guild_states.find(event.command.guild_id)->cmd_handler.handle<slashcommand_t>(event);
    });
    bot.on_user_context_menu([&guild_states](const user_context_menu_t& event){
        guild_states.find(event.command.guild_id)->cmd_handler.handle<user_context_menu_t>(event);
    });
    bot.on_message_context_menu([&guild_states](const message_context_menu_t& event){
        guild_states.find(event.command.guild_id)->cmd_handler.handle<message_context_menu_t>(event);
    });

    bot.on_ready([](const dpp::ready_t&){
    });

    bot.start(dpp::st_return);

    fut.wait();
    bot.shutdown();
    for (auto [_, state_ptr] : guild_states.get_container()) {
        guild_states.remove(state_ptr);
    }
    }
    std::this_thread::sleep_for(chr::minutes{1});
    //TODO: properly join all the threads before going out of scope
}
