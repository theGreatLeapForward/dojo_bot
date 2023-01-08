//
// Created by thegreatleapforward on 03/01/23.
//
#include <dpp/dpp.h>
#include <iostream>
#include <set>

/*class message_collector_t : dpp::message_collector {
public:
    message_collector_t(dpp::cluster* cl, dpp::cache<dpp::message>* c, std::vector<dpp::snowflake>* v, std::promise<void>&& p)
    : dpp::message_collector(cl, std::chrono::seconds::max().count()), cache{c}, id_vec{v}, promise{std::move(p)} {
        std::cout << "collector created!\n";
    }

private:
    const long time = std::chrono::duration_cast<std::chrono::seconds>((std::chrono::system_clock::now() - std::chrono::weeks{1}).time_since_epoch()).count();
    dpp::cache<dpp::message>* cache;
    std::vector<dpp::snowflake>* id_vec;
    std::promise<void> promise;

    void completed(const std::vector<dpp::message>& list) override {
        for (const auto& msg: list) {
            id_vec->emplace_back(msg.id);
            auto* to_save = new dpp::message{msg};
            cache->store(to_save);
        }

        promise.set_value();
    }

    const dpp::message* filter(const dpp::message_create_t& event) override {
        if (static_cast<long>(event.msg.id.get_creation_time()) < time) {
            return nullptr;
        }
        std::cout << event.msg.content << std::endl;
        return &event.msg;
    }
};*/

struct msgs_and_ids {
    dpp::cluster* owner;
    dpp::cache<dpp::message> messages;

    std::set<dpp::snowflake> message_ids;

    void insert(const dpp::message& msg) {
        if (this->message_ids.emplace(msg.id).second) {
            this->messages.store(new dpp::message{msg}); //move here has no benefit apparently
            //owner->log(dpp::ll_info, msg.content);
        }
    }

    template<class Rep, class Period>
    void gc(const std::chrono::duration<Rep, Period>& offset) {
        const auto time = (std::chrono::system_clock::now() - offset).time_since_epoch();
        auto num = std::chrono::duration_cast<std::chrono::seconds>(time).count() << 22;

        std::erase_if(this->message_ids, [this, &time](dpp::snowflake id) -> bool {
            //TODO this can bbe optimized by taking advantage of the fact that sets are sorted
            if (std::chrono::seconds{static_cast<long>(id.get_creation_time())} < time) {
                this->messages.remove(this->messages.find(id));
                return true;
            }
            return false;
        });
    }
};

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

    const std::array<dpp::snowflake, 1> guild_ids {820855382472785921};

    dpp::cluster bot(BOT_TOKEN, dpp::i_message_content | dpp::i_default_intents | dpp::i_guild_members);

    msgs_and_ids messages{.owner = &bot};

    bot.on_log(dpp::utility::cout_logger());

    bot.on_guild_create([&guild_ids, &bot](const dpp::guild_create_t& event){
        const auto id = event.created->id;
        if (std::find(guild_ids.cbegin(), guild_ids.cend(), id) == guild_ids.cend()) {
            bot.current_user_leave_guild(id);
        }
    });

    //probably don't need to actually cache the messages, just keep a running total
    bot.on_message_create([&bot, &messages](const dpp::message_create_t& event){
        messages.insert(event.msg);

        if (dpp::run_once<struct get_some_past_messages>()) {
            bot.current_user_get_guilds([&bot, id = event.msg.id, &messages](const auto& event){
                if (event.is_error()) {
                    bot.log(dpp::ll_error, event.get_error().message);
                }
                else {
                    bot.log(dpp::ll_info, "GUILDS GOT");

                    auto guild_map = event.template get<dpp::guild_map>();
                    bot.log(dpp::ll_debug, std::to_string(guild_map.size()));

                    for (const auto& [guild_id, guild]: guild_map) {
                        bot.log(dpp::ll_debug, "Guild channels count: " + std::to_string(guild.channels.size()));
                        bot.log(dpp::ll_debug, "Guild channels count: " + std::to_string(bot.channels_get_sync(guild_id).size()));

                        auto channels = guild.channels;
                        channels.emplace_back(893595390241280022);

                        for (auto channel_id: channels) {
                            bot.log(dpp::ll_debug, std::to_string(channel_id));
                            //bot.log(dpp::ll_info, "Setting up callback for channel " + std::to_string(channel_id));

                            bot.messages_get(channel_id, 0, id, 0, 100, [&bot, &messages](const auto &event) {
                                if (event.is_error()) {
                                    bot.log(dpp::ll_error, event.get_error().message);
                                } else {
                                    bot.log(dpp::ll_info, "CHANNELS GOT");

                                    for (const auto &[message_id, message]: event.template get<dpp::message_map>()) {
                                        messages.insert(message);
                                    }
                                }
                            });
                        }
                    }
                }
            });
        }
    });

    /*bot.on_message_delete([&messages](const dpp::message_delete_t& event){
        //make this conditional?
        messages.remove(messages.find(event.deleted->id));
    });*/

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        auto name = event.command.get_command_name();
        if (name == "ping") {
            event.reply("Pong!");
        }
    });

    /*std::promise<void> promise;
    auto fut = promise.get_future();
    auto collector = message_collector_t{&bot, &messages, &message_ids, std::move(promise)};*/

    bot.on_ready([&bot, &messages](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            bot.global_bulk_command_create(std::vector{
                dpp::slashcommand("ping", "Ping pong!", bot.me.id),
            });

            //garbage collection
            bot.start_timer(dpp::timer_callback_t{[&messages](auto param){
                messages.gc(std::chrono::weeks{1});
            }}, duration_cast<std::chrono::seconds>(std::chrono::days{1}).count());

        }
    });

    bot.start(dpp::st_wait);
}
