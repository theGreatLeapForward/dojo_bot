//
// Created by thegreatleapforward on 12/01/23.
//

#include "commands.h"

const static snowflake DEV_ID = 762155750403342358;

void sc_rolelist(const dpp::slashcommand_t& event, guild_state&) {
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

void sc_user_activity(const dpp::slashcommand_t& event, guild_state&) {
    auto embed = dpp::embed().set_title("Todo");

    event.reply(message{event.command.channel_id, embed});
    //TODO
}

void sc_channel_activity(const dpp::slashcommand_t& event, guild_state& ref) {
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

                if (ref.dojo_info.has_value()) {

                    auto& cat = ref.dojo_info.value().categories_to_users;
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

void ctx_user_activity(const dpp::user_context_menu_t& event, guild_state&) {
    auto embed = dpp::embed().set_title("Todo");

    event.reply(message{event.command.channel_id, embed});
    //TODO
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

void category_clear(snowflake category, guild_state& ref) {}

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
