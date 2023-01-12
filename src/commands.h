//
// Created by thegreatleapforward on 11/01/23.
//

#pragma once
#include "backend.h"
#include <concepts>
#include <dpp/dpp.h>
#include <functional>

using namespace std::chrono_literals;
namespace ran = std::ranges;
namespace chr = std::chrono;
using dpp::slashcommand;
using dpp::snowflake;
using dpp::message;
using dpp::slashcommand_t;
using dpp::user_context_menu_t;
using dpp::message_context_menu_t;

template<typename T>
using cmd_func = std::function<void(const T&, state_ref)>;

dpp::command_option add_choices(dpp::command_option option, coc_vec&& choices) {
    if (option.autocomplete) {
        throw dpp::logic_exception("Can't set autocomplete=true if choices exist in the command_option");
    }
    std::move(choices.begin(), choices.end(), std::back_inserter(option.choices));
    return option;
}

const static snowflake DEV_ID = 762155750403342358;

void sc_rolelist(const dpp::slashcommand_t& event, state_ref) {
    auto role_id = std::get<snowflake>(event.get_parameter("role"));
    auto embed = dpp::embed().
            set_title(fmt::format("All users with role {}", dpp::find_role(role_id)->get_mention()));

    auto& members = dpp::find_guild(event.command.guild_id)->members;
    for (const auto& [member_id, member]: members) {
        if (ran::find(member.roles, role_id) != member.roles.end()) {
            embed.add_field(member.get_mention(), "todo", true);
        }
    }
    event.reply(message{event.command.channel_id, embed});
}

void sc_user_activity(const dpp::slashcommand_t& event, state_ref) {
    auto embed = dpp::embed().set_title("Todo");

    event.reply(message{event.command.channel_id, embed});
    //TODO
}

void sc_channel_activity(const dpp::slashcommand_t& event, state_ref ref) {
    event.thinking(true);
    constexpr int preview_size = 5;
    constexpr int max_msg_size = 100;

    ref.bot.messages_get(event.command.channel_id, 0, 0, 0, preview_size,
    [&event, &ref](const dpp::confirmation_callback_t& conf){
        if (!conf.is_error()) {
            auto param = event.get_parameter("channel");
            auto id = event.command.channel_id;
            if (std::holds_alternative<snowflake>(param)) {
                try {
                    id = std::get<snowflake>(param);
                }
                catch (std::bad_variant_access& ex) {
                    ref.bot.log(dpp::ll_error, ex.what());
                }
            }
            auto channel = dpp::find_channel(id);
            if (channel->is_category()) {
                event.reply("Categories are not yet supported."); //TODO
                return;
            }

            auto embed = dpp::embed().set_color(0xA020F0).
                    set_title(fmt::format("Recent activity in channel {}", channel->get_mention()));

            auto& parent_name = dpp::find_channel(channel->parent_id)->name;
            auto it = ref.dojo_infos.find(event.command.guild_id);
            if (it != ref.dojo_infos.end()) {
                auto& cat = it->second.categories_to_users;
                auto cat_it = cat.find(channel->parent_id);
                if (cat_it != cat.end()) {
                    embed.add_field("Channel ownership",
                                    fmt::format("Channel in category {} owned by {}",
                                                parent_name,
                                                dpp::find_user(cat_it->second)->get_mention()));
                }
                else {
                    embed.add_field("Channel ownership",
                                    fmt::format("Channel in unowned category {}", parent_name));
                }
            }
            else {
                embed.add_field("Category", fmt::format("Channel in category {}", parent_name));
            }

            embed.add_field("Channel preview", fmt::format("Most recent messages sent in {}", channel->name));

            auto map = conf.get<dpp::message_map>();
            std::vector<message> out;
            out.reserve(map.size());
            ran::transform(std::move(map), out.begin(), [](auto& y){return y.second;});
            ran::sort(out, [](auto& x, auto& y){return x.id > y.id;});
            for (auto& msg: out) {
                if (msg.content.length() > max_msg_size) {
                    msg.content.resize(max_msg_size);
                    msg.content += "...";
                }

                embed.add_field(fmt::format("From: {}", msg.author.get_mention()),
                                msg.content);
            }

            event.reply(message{id, embed});
        }
        else {
            ref.bot.log(dpp::ll_error, conf.get_error().message);
        }
    });
}

void ctx_user_activity(const dpp::user_context_menu_t& event, state_ref) {
    auto embed = dpp::embed().set_title("Todo");

    event.reply(message{event.command.channel_id, embed});
    //TODO
}

void sc_restart(const dpp::slashcommand_t& event, state_ref ref) {
    if (event.command.member.user_id == DEV_ID) {
        event.reply("Am die!");
        ref.exec_stop.set_value();
    }
    else {
        event.thinking(true);
    }

}

void sc_debug(const dpp::slashcommand_t& event, state_ref ref) {
    auto guild = dpp::find_guild(event.command.guild_id);
    for (auto channel_id: guild->channels) {
        auto channel = dpp::find_channel(channel_id);
        ref.bot.log(dpp::ll_debug, channel->name);
        ref.bot.log(dpp::ll_info, std::to_string(channel_id));
    }
    event.reply("Debug complete");
}

void sc_set_category(const dpp::slashcommand_t& event, state_ref ref) {
    event.thinking(true);
    //TODO
}

void sc_help(const dpp::slashcommand_t& event, state_ref ref) {
    event.thinking(true);
    //TODO
}

void sc_guides(const dpp::slashcommand_t& event, state_ref ref) {
    event.thinking(true);
    //TODO
}

void sc_scorecounting(const dpp::slashcommand_t& event, state_ref ref) {
    event.thinking(true);
    //TODO
}

template<typename T, typename ... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <typename ...Args>
struct command_handler {
    command_handler(dpp::cluster* owner_param, snowflake id_param, state_ref ref_param)
            : owner(owner_param), id(id_param), ref(ref_param) {
        owner->log(dpp::ll_info, fmt::format("command_handler created for {}", id));
    }

    dpp::cluster* const owner;
    const snowflake id;
    state_ref ref;

    std::shared_mutex cmds_mutex;
    std::tuple<id_map<cmd_func<Args>> ...> cmd_funcs;
    std::map<snowflake, slashcommand> commands;

    std::atomic<bool> called = false;

    command_handler& default_cmds() {
        using sc = slashcommand;
        using co = dpp::command_option;

        add_command<slashcommand_t>(
            sc{"rolelist", "get all users with specified role", id}.
            add_option(co{dpp::co_role, "role", "role to get users with", true}),
            sc_rolelist);
        add_command<slashcommand_t>(
            sc{"user-activity", "what are you up to?", id}.
            add_option(co{dpp::co_user, "user", "user who's activity to check", false}),
            sc_channel_activity);
        add_command<user_context_menu_t>(
            sc{"activity", "what are you up to?", id}.
            set_type(dpp::ctxm_user),
            ctx_user_activity);
        add_command<slashcommand_t>(
            sc{"channel-activity", "what is going on in here?", id}.
            add_option(co{dpp::co_channel, "channel", "channel who's activity to check", false}),
            sc_channel_activity);
        add_command<slashcommand_t>(
            sc{"set-category-owner", "sets a category as owned by a user", id}.
            add_option(co{dpp::co_user, "user", "user to set as owner, defaults to you", false}.
            add_channel_type(dpp::CHANNEL_CATEGORY)).
            set_default_permissions(dpp::p_manage_channels),
            sc_set_category);
        add_command<slashcommand_t>(
            sc("debug", "running this a lot will crash the bot", id).
            set_default_permissions(dpp::p_administrator),
            sc_debug);
        add_command<slashcommand_t>(
            sc("stop", "[EMERGENCY]: stops the bot", id).
            set_default_permissions(dpp::p_administrator),
            sc_restart);
        add_command<slashcommand_t>(
            sc{"help", "help", id}.
            add_option(co{dpp::co_string, "command", "optional: specific command to get help for", false}),
            sc_help);
        add_command<slashcommand_t>(
            sc{"guides", "retrieves a guide", id}.
            add_option(add_choices(co{dpp::co_string, "guide", "which guide to retrieve", true}, guilds_options())),
            sc_guides);

        return *this;
    }

    command_handler& dojo_cmds() {
        using sc = slashcommand;

        add_command<slashcommand_t>(sc{"score-count-helper", "score counting help", id}, sc_scorecounting);

        return *this;
    }

    void register_cmds() {
        if (!called) {
            called = true;

            std::vector<slashcommand> cmds;
            cmds.resize(commands.size());
            std::transform(commands.cbegin(), commands.cend(), std::back_inserter(cmds), [](const auto& y){
                return y.second;
            });

            owner->guild_bulk_command_create(cmds, id);
        }
        else {
            throw dpp::logic_exception(fmt::format(
                    "register_cmds called a 2nd time on a command handler for guild {}", id));
        }
    }

    template<typename T>
    requires IsAnyOf<T, Args...>
    command_handler& add_command(const dpp::slashcommand& cmd, cmd_func<T>&& func) {
        {
            auto lock = std::unique_lock(cmds_mutex);
            commands.emplace(cmd.id, cmd);
            std::get<id_map<cmd_func<T>>>(cmd_funcs).emplace(cmd.id, func);
        }

        if (!called) {
            owner->guild_command_create(cmd, id);
        }
        return *this;
    }

    template<typename T>
    requires IsAnyOf<T, Args...>
    inline void handle(const T& event) {
        std::invoke(std::get<id_map<cmd_func<T>>>(cmd_funcs).at(event.command.id), event, ref);
    }
};
