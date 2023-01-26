//
// Created by thegreatleapforward on 12/01/23.
//

#include "commands.h"

const static snowflake DEV_ID = 762155750403342358;
constexpr auto max_msg_size = 100;

void sc_rolelist(const dpp::slashcommand_t& event, guild_state&) {
    event.thinking();
    auto role_id = std::get<snowflake>(event.get_parameter("role"));
    auto role = *dpp::find_role(role_id);
    auto embed = dpp::embed().
            set_title(fmt::format("All users with role {}", role.name));
    embed.add_field("", role.get_mention());

    auto& members = dpp::find_guild(event.command.guild_id)->members;

    std::string str;
    for (const auto& [member_id, member]: members) {
        if (ran::find(member.roles, role_id) != member.roles.end()) {
            str += fmt::format("{}, ", member.get_mention());
        }
    }
    embed.add_field("", str);

    event.edit_response(message{event.command.channel_id, embed});
}

dpp::embed impl_user_activity(const dpp::user& user, guild_state& ref) {
    auto embed = dpp::embed().set_title("User Activity");

    embed.add_field("Recent Activity", fmt::format("Most recent messages sent by {}", user.get_mention()));

    auto it = ref.msg_cache.user_msgs.find(user.id);
    if (it == ref.msg_cache.user_msgs.end()) {
        ref.bot.log(dpp::ll_error, "call to user_msgs.find in impl_user_activity returned past-the-end iter");
        embed.add_field("ERROR", "Critical backend error. Please try again.");
        return embed;
    }
    auto queue = it->second;

    while (!queue.empty()) {
        auto msgptr = ref.msg_cache.messages.find(queue.top());
        if (!msgptr) {
            ref.bot.log(dpp::ll_error, "call to messages.find in impl_user_activity returned nullptr");
            embed.add_field("ERROR", "Critical backend error. Please try again.");
            return embed;
        }
        auto msg = *msgptr;

        if (msg.content.length() > max_msg_size) {
            msg.content.resize(max_msg_size - 3);
            msg.content += "...";
        }

        embed.add_field(fmt::format("In: {}", dpp::find_channel(msg.channel_id)->name),
                        fmt::format("https://discord.com/channels/{}/{}\n{}", msg.channel_id, msg.id, msg.content), true);
        queue.pop();
    }

    return embed;
}

void sc_user_activity(const dpp::slashcommand_t& event, guild_state& ref) {
    event.thinking();
    auto param = event.get_parameter("user");
    auto user = event.command.get_issuing_user();
    if (std::holds_alternative<snowflake>(param)) {
        auto id = dpp::find_user(std::get<snowflake>(param));
        if (!id) {
            ref.bot.log(dpp::ll_error, "call to find_user for sc_user_activity returned null");
            return;
        }
        user = *id;
    }

    event.edit_response(message{event.command.channel_id, impl_user_activity(user, ref)});
}

void ctx_user_activity(const dpp::user_context_menu_t& event, guild_state& ref) {
    event.thinking();
    event.edit_response(message{event.command.channel_id,
                        impl_user_activity(event.get_user(), ref)});
}

void sc_channel_activity(const dpp::slashcommand_t& event, guild_state& ref) {
    event.thinking();
    constexpr int preview_size = 5;

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

    ref.bot.messages_get(id, 0, 0, 0, preview_size,
        [event, &ref, id](const dpp::confirmation_callback_t& conf){
            if (!conf.is_error()) {
                auto channel = dpp::find_channel(id);
                if (!channel) {
                    ref.bot.log(dpp::ll_error, "call to find_channel in messages_get in channel_activity returned nullptr");
                    return;
                }

                auto embed = dpp::embed().set_color(0xA020F0).
                        set_title(fmt::format("Recent activity in {}", channel->name));

                auto& parent_name = dpp::find_channel(channel->parent_id)->name;

                if (ref.dojo_info.has_value()) {

                    auto& cat = ref.dojo_info.value().categories_to_users;
                    auto cat_it = cat.find(channel->parent_id);
                    if (cat_it != cat.end()) {
                        embed.add_field("Channel ownership",
                                        fmt::format("{} is in the category {} owned by {}",
                                                    channel->name,
                                                    parent_name,
                                                    dpp::find_user(cat_it->second)->get_mention()));
                    }
                    else {
                        embed.add_field("Channel ownership",
                                        fmt::format("{} is in unowned category {}", channel->name, parent_name));
                    }
                }

                embed.add_field("Channel preview", fmt::format("Recent messages sent in {}", channel->get_mention()));

                auto map = conf.get<dpp::message_map>();
                auto vals = map | std::views::values;
                auto vec = std::vector(ran::begin(vals), ran::end(vals));
                ran::sort(vec, [](auto& x, auto& y){return x.id > y.id;});

                for (auto& msg: vals) {
                    if (msg.content.length() > max_msg_size) {
                        msg.content.resize(max_msg_size - 3);
                        msg.content += "...";
                    }

                    embed.add_field(fmt::format("From: {}", msg.author.username),
                                    msg.content, true);
                }

                event.edit_response(message{id, embed});
            }
            else {
                ref.bot.log(dpp::ll_error, conf.get_error().message);
            }
        });
}

void sc_restart(const dpp::slashcommand_t& event, guild_state& ref) {
    if (event.command.member.user_id == DEV_ID) {
        event.reply("Am die!");
        ref.exec_stop.set_value();
    }
    else {
        event.thinking(true);
    }

}

void sc_debug(const dpp::slashcommand_t& event, guild_state& ref) {
    if (event.command.member.user_id == DEV_ID) {
        event.reply("Debug complete");
    }
    else {
        event.thinking(true);
    }
}

void sc_category(const dpp::slashcommand_t& event, guild_state& ref) {
    event.thinking(true);
    //TODO
}

void category_clear(snowflake category, guild_state& ref) {
}

void user_category_clear(snowflake user, guild_state& ref) {}

void sc_help(const dpp::slashcommand_t& event, guild_state& ref) {
    event.thinking(true);
    //TODO
}

void sc_guides(const dpp::slashcommand_t& event, guild_state& ref) {
    event.thinking(true);
    //TODO
}

void sc_scorecounting(const dpp::slashcommand_t& event, guild_state& ref) {
    event.thinking(true);
    //TODO
}

void sc_belt(const slashcommand_t &event, guild_state &) {
    event.thinking(true);
}

void ctx_belt_upgrade(const user_context_menu_t &, guild_state &) {

}

