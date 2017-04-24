
#include "global.hpp"

#define _USE_MATH_DEFINES

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>

#include "messages.hpp"
#include "pugixml.hpp"
#include "debug.hpp"

namespace jup {

/**
 * Helper function to call the std::distance with a range.
 */
template <typename Range>
auto distance(Range const& range) {
	return std::distance(range.begin(), range.end());
}

/**
 * Helper classes for writing the xml output somewhere
 */
struct Socket_writer: public pugi::xml_writer {
	Socket_writer(Socket* sock): sock{sock} {assert(sock);}

	void write(void const* data, std::size_t size) override {
		sock->send(Buffer_view {data, (int)size});
	}

	Socket* sock;
};
struct Buffer_writer: public pugi::xml_writer {
	Buffer_writer(Buffer* buf): buf{buf} {assert(buf);}

	void write(void const* data, std::size_t size) override {
		buf->append(data, size);
	}

	Buffer* buf;
};

// This buffer contains all the memory that is needed for the messages. It
// should not relocate during operations! (This is not optimal. However, the
// maximum amount needed for parsing a message should stay constant.)
static Buffer memory_for_messages;

// The additional data behind a message (that belongs to the next message in the socket)
static Buffer additional_data;

static int idmap_offset;
static int idmap16_offset;

// When pugi requests more Memory than memory_for_messages contains, is gets
// some dynamically. This remembers the amount, so that the buffer will be
// resized the next time around.
static int additional_buffer_needed = 0;

// Similar to memory_for_messages, this holds all memory needed to map the
// strings to ids. It is not cleared between messages. There is no need to not
// relocate this.
static Buffer memory_for_strings;

// If this is not null, each message will be dumped in xml form into the stream.
static std::ostream* dump_xml_output;

/**
 * Implement the pugi memory function. This tries to get the memory from
 * memory_for_messages. If that fails due to not enough space the memory is
 * allocated dynamically.
 */
void* allocate_pugi(size_t size) {
	// pugi wants its memory aligned, so try to keep it happy.
	constexpr static int align = alignof(double) > alignof(void*)
		? alignof(double) : alignof(void*);
	void* result = memory_for_messages.end();
	std::size_t space = memory_for_messages.space();
	if (std::align(align, size, result, space)) {
		memory_for_messages.resize((char*)result + size - memory_for_messages.begin());
		return result;
	} else {
		jerr << "Warning: Buffer for pugi is not big enough, need additional " << size << '\n';
    	additional_buffer_needed += size;
    	return malloc(size);
	}
}
void deallocate_pugi(void* ptr) {
	// If the memory is inside the buffer, ignore the deallocation. The buffer is
	// cleared anyways.
	if (memory_for_messages.begin() > ptr or ptr >= memory_for_messages.end()) {
		free(ptr);
	}
}


auto& idmap() {
    return memory_for_strings.get<Flat_idmap>(idmap_offset);
}
auto& idmap16() {
    return memory_for_strings.get<Flat_idmap_base<u16, u16, 4096>>(idmap16_offset);
}

// see header
void init_messages(std::ostream* _dump_xml_output) {
	pugi::set_memory_management_functions(&allocate_pugi, &deallocate_pugi);
    // 101k is currently used when parsing a perception.
	memory_for_messages.reserve(256 * 1024);

	// This is just for performance
	memory_for_strings.reserve(2048);
    
    idmap_offset = memory_for_strings.size();
	memory_for_strings.emplace_back<Flat_idmap>();
    idmap16_offset = memory_for_strings.size();
	memory_for_strings.emplace_back<Flat_idmap_base<u16, u16, 4096>>();
	
	// Guarantee that no id maps to zero
	assert(idmap()  .get_id("", &memory_for_strings) == 0);
	assert(idmap16().get_id("", &memory_for_strings) == 0);

    dump_xml_output = _dump_xml_output;
}

/**
 * Map the str to an id. If the str is not already mapped a new id will be
 * generated.
 */
u8  get_id(Buffer_view str) {
	return idmap()  .get_id(str, &memory_for_strings);
}
u16 get_id16(Buffer_view str) {
	return idmap16().get_id(str, &memory_for_strings);
}
u8  register_id  (Buffer_view str) { return get_id  (str); }
u16 register_id16(Buffer_view str) { return get_id16(str); }

// see header
u8  get_id_from_string  (Buffer_view str) { return idmap()  .get_id(str); }
u16 get_id_from_string16(Buffer_view str) { return idmap16().get_id(str); }

// see header
Buffer_view get_string_from_id(u8  id) { return idmap()  .get_value(id); }
Buffer_view get_string_from_id(u16 id) { return idmap16().get_value(id); }

/**
 * Construct the Pos object from the coordinates in xml_obj
 */
Pos get_pos(pugi::xml_node xml_obj) {
	constexpr static double pad = lat_lon_padding;
	double lat = xml_obj.attribute("lat").as_double();
	double lon = xml_obj.attribute("lon").as_double();
	double lat_diff = (map_max_lat - map_min_lat);
	double lon_diff = (map_max_lon - map_min_lon);
	lat = (lat - map_min_lat + lat_diff * pad) / (1 + 2*pad) / lat_diff;
	lon = (lon - map_min_lon + lon_diff * pad) / (1 + 2*pad) / lon_diff;
	assert(0.0 <= lat and lat < 1.0);
	assert(0.0 <= lon and lon < 1.0);
	return Pos {(u16)(lat * 65536.0 + 0.5), (u16)(lon * 65536.0 + 0.5)};
}

/**
 * Map the Pos back to coordinates and write them into an pugi::xml_node
 */
void set_xml_pos(Pos pos, pugi::xml_node* into) {
	assert(into);

	auto back = get_pos_back(pos);
	into->append_attribute("lat") = back.first;
	into->append_attribute("lon") = back.second;
}


void parse_auth_response(pugi::xml_node xml_obj, Buffer* into) {
	assert(into);
	auto& mess = into->emplace_back<Message_Auth_Response>();
	auto succeeded = xml_obj.attribute("result").value();
	if (std::strcmp(succeeded, "ok") == 0) {
		mess.succeeded = true;
	} else if (std::strcmp(succeeded, "fail") == 0) {
		mess.succeeded = false;
	} else {
		assert(false);
	}
}

void parse_sim_start(pugi::xml_node xml_obj, Buffer* into) {
	assert(into);

	int prev_size = into->size();
	int space_needed = sizeof(Message_Sim_Start);
	{
		constexpr int s = sizeof(u8);
		space_needed += s + sizeof(u8) * distance(xml_obj.child("role").children("tool"));
		space_needed += s;
		for (auto xml_item: xml_obj.children("item")) {
			space_needed += s * 2 + sizeof(Item)
				+ sizeof(Item_stack) * distance(xml_item.children("item"))
				+ sizeof(u8) * distance(xml_item.children("tool"));
		}
	}
	into->reserve_space(space_needed);
	into->trap_alloc(true);
	
	auto& sim = into->emplace_back<Message_Sim_Start>().simulation;
	sim.id = get_id(xml_obj.attribute("id").value());
	sim.map = get_id(xml_obj.attribute("map").value());
	narrow(sim.seed_capital, xml_obj.attribute("seedCapital").as_int());
	narrow(sim.steps,        xml_obj.attribute("steps")      .as_int());
	sim.team = get_id(xml_obj.attribute("team").value());

	auto xml_role = xml_obj.child("role");
	sim.role.name = get_id(xml_role.attribute("name").value());
	narrow(sim.role.speed,   xml_role.attribute("speed")  .as_int());
	narrow(sim.role.battery, xml_role.attribute("battery").as_int());
	narrow(sim.role.load,    xml_role.attribute("load")   .as_int());
	sim.role.tools.init(into);
	for (auto xml_tool: xml_role.children("tool")) {
		u8 name = get_id(xml_tool.attribute("name").value());
		sim.role.tools.push_back(name, into);
	}

	sim.items.init(into);
	for (auto xml_item: xml_obj.children("item")) {
		Item item;
		item.name = get_id(xml_item.attribute("name").value());
		narrow(item.volume, xml_item.attribute("volume").as_int());
		sim.items.push_back(item, into);
	}
	Item* item = sim.items.begin();
	for (auto xml_item: xml_obj.children("item")) {
		assert(item != sim.items.end());
		item->consumed.init(into);
		for (auto xml_item : xml_item.children("item")) {
			Item_stack stack;
			stack.item = get_id(xml_item.attribute("name").value());
			narrow(stack.amount, xml_item.attribute("amount").as_int());
			item->consumed.push_back(stack, into);
		}
		item->tools.init(into);
		for (auto xml_tool : xml_item.children("tool")) {
			item->tools.push_back(get_id(xml_tool.text().get()), into);
		}
		++item;
	}
	assert(item == sim.items.end());
	
	into->trap_alloc(false);
	assert(into->size() - prev_size == space_needed);
}

void parse_sim_end(pugi::xml_node xml_obj, Buffer* into) {
	assert(into);
	auto& mess = into->emplace_back<Message_Sim_End>();
	narrow(mess.ranking, xml_obj.attribute("ranking").as_int());
	narrow(mess.score,   xml_obj.attribute("score")  .as_int());
}

void parse_request_action(pugi::xml_node xml_perc, Buffer* into) {
	assert(into);
	auto xml_self = xml_perc.child("self");
	auto xml_action = xml_self.child("action");

	int prev_size = into->size();
	int space_needed = sizeof(Message_Request_Action);
	{
		constexpr int s = sizeof(u8);
		space_needed += s + sizeof(Item_stack) * distance(xml_self.children("items"));
		space_needed += s + sizeof(Entity) * distance(xml_perc.children("entity"));

		space_needed += 6 * s
			+ sizeof(Charging_station) * distance(xml_perc.children("chargingStation"))
			+ sizeof(Dump)             * distance(xml_perc.children("dump"))
			+ sizeof(Shop)             * distance(xml_perc.children("shop"))
			+ sizeof(Storage)          * distance(xml_perc.children("storage"))
			+ sizeof(Workshop)         * distance(xml_perc.children("workshop"))
			+ sizeof(Resource_node)    * distance(xml_perc.children("resourceNode"));
        
		for (auto xml_fac : xml_perc.children("shop")) {
			space_needed += s + sizeof(Shop_item) * distance(xml_fac.children("item"));
		}
		for (auto xml_fac : xml_perc.children("storage")) {
			space_needed += s + sizeof(Storage_item) * distance(xml_fac.children("item"));
		}

		space_needed += 4 * s;
		for (auto xml_job : xml_perc.children("auction")) {
			space_needed += sizeof(Auction) + s
				+ sizeof(Item_stack) * distance(xml_job.children("required"));
		}
		for (auto xml_job : xml_perc.children("job")) {
			space_needed += sizeof(Job) + s
				+ sizeof(Item_stack) * distance(xml_job.children("required"));
		}
		for (auto xml_job : xml_perc.children("mission")) {
			space_needed += sizeof(Mission) + s
				+ sizeof(Item_stack) * distance(xml_job.children("required"));
		}
		for (auto xml_job : xml_perc.children("posted")) {
			space_needed += sizeof(Posted) + s
				+ sizeof(Item_stack) * distance(xml_job.children("required"));
		}
	}
	
	into->reserve_space(space_needed);
	into->trap_alloc(true);

	auto& perc = into->emplace_back<Message_Request_Action>().perception;

	narrow(perc.deadline, xml_perc.attribute("deadline").as_ullong());
	narrow(perc.id,       xml_perc.attribute("id")      .as_int());
	narrow(perc.simulation_step,
		   xml_perc.child("simulation").attribute("step").as_int());
	
	auto& self = perc.self;
	self.name = get_id(xml_self.attribute("name").value());
	self.team = get_id(xml_self.attribute("team").value());
	self.pos = get_pos(xml_self);
	self.role = get_id(xml_self.attribute("role").value());
	narrow(self.charge, xml_self.attribute("charge").as_int());
	narrow(self.load, xml_self.attribute("load").as_int());
	self.last_action = Action::get_id(xml_action.attribute("type").value());
	self.last_action_result = Action::get_result_id(
		xml_action.attribute("result").value());
	self.items.init(into);
	for (auto xml_item: xml_self.children("items")) {
		Item_stack item;
		item.item = get_id(xml_item.attribute("name").value());
		if (xml_item.attribute("amount").as_int() > 254) {
			item.amount = 254;
		} else {
			narrow(item.amount, xml_item.attribute("amount").as_int());
		}
		self.items.push_back(item, into);
	}

	narrow(perc.team_money,
		xml_perc.child("team").attribute("money").as_int());

	perc.entities.init(into);
	for (auto xml_ent: xml_perc.children("entity")) {
		Entity ent;
		ent.name = get_id(xml_ent.attribute("name").value());
		ent.team = get_id(xml_ent.attribute("team").value());
		ent.pos =  get_pos(xml_ent);
		ent.role = get_id(xml_ent.attribute("role").value());
		perc.entities.push_back(ent, into);
	}
	
	perc.charging_stations.init(into);
	for (auto xml_fac: xml_perc.children("chargingStation")) {
		Charging_station fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		narrow(fac.rate,  xml_fac.attribute("rate") .as_int());
		perc.charging_stations.push_back(fac, into);
	}
	perc.dumps.init(into);
	for (auto xml_fac: xml_perc.children("dump")) {
		Dump fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		perc.dumps.push_back(fac, into);
	}
    
	perc.shops.init(into);
	for (auto xml_fac: xml_perc.children("shop")) {
		Shop fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		fac.restock = get_id(xml_fac.attribute("restock").value());
		perc.shops.push_back(fac, into);
	}	
	Shop* shop = perc.shops.begin();
	for (auto xml_fac : xml_perc.children("shop")) {
		assert(shop != perc.shops.end());
		shop->items.init(into);
		for (auto xml_item : xml_fac.children("item")) {
			Shop_item item;
			item.item = get_id(xml_item.attribute("name").value());
			if (xml_item.attribute("amount").as_int() > 254) {
				item.amount = 254;
			} else {
				narrow(item.amount, xml_item.attribute("amount").as_int());
			}
			if (xml_item.attribute("price").as_int() > 65535) {
				item.cost = 65535;
			} else {
				narrow(item.cost, xml_item.attribute("price").as_int());
			}
			shop->items.push_back(item, into);
		}
		++shop;
	}
	assert(shop == perc.shops.end());
    
	perc.storages.init(into);
	for (auto xml_fac: xml_perc.children("storage")) {
		Storage fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
        narrow(fac.total_capacity, xml_fac.attribute("totalCapacity").as_int());
        narrow(fac.used_capacity,  xml_fac.attribute("usedCapacity") .as_int());
        
		perc.storages.push_back(fac, into);
	}
	Storage* storage = perc.storages.begin();
	for (auto xml_fac : xml_perc.children("storage")) {
		assert(storage != perc.storages.end());
		storage->items.init(into);
		for (auto xml_item : xml_fac.children("item")) {
			Storage_item item;
			item.item = get_id(xml_item.attribute("name").value());
			narrow(item.amount, xml_item.attribute("stored").as_int());
			narrow(item.delivered, xml_item.attribute("delivered").as_int());
			storage->items.push_back(item, into);
		}
		++storage;
	}
	assert(storage == perc.storages.end());
    
	perc.workshops.init(into);
	for (auto xml_fac: xml_perc.children("workshop")) {
		Workshop fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		perc.workshops.push_back(fac, into);
	}

	perc.resource_nodes.init(into);
	for (auto xml_res : xml_perc.children("resourceNode")) {
		Resource_node res;
		res.name = get_id(xml_res.attribute("name").value());
		res.pos = get_pos(xml_res);
		res.resource = get_id(xml_res.attribute("resource").value());
		perc.resource_nodes.push_back(res, into);
	}

	perc.auctions.init(into);
	for (auto xml_job: xml_perc.children("auction")) {
		Auction job;
		job.id      = get_id16(xml_job.attribute("id")     .value());
		job.storage = get_id  (xml_job.attribute("storage").value());
		narrow(job.start,        xml_job.attribute("start") .as_int());
		narrow(job.end,          xml_job.attribute("end").as_int());
		narrow(job.reward,       xml_job.attribute("reward").as_int());
		narrow(job.auction_time, xml_job.attribute("auctionTime").as_int());
		narrow(job.fine,         xml_job.attribute("fine")  .as_int());
		narrow(job.max_bid,      xml_job.attribute("maxBid").as_int());
		perc.auctions.push_back(job, into);
	}
	Auction* joba = perc.auctions.begin();
	for (auto xml_job: xml_perc.children("auction")) {
		assert(joba != perc.auctions.end());
		joba->required.init(into);
		for (auto xml_item: xml_job.children("required")) {
			Item_stack item;
			item.item = get_id(xml_item.attribute("name").value());
            if (xml_item.attribute("amount")   .as_int() > 254) {
				item.amount = 254;
            } else {
                narrow(item.amount,    xml_item.attribute("amount")   .as_int());
            }
			joba->required.push_back(item, into);
		}
		++joba;
	}
	assert(joba == perc.auctions.end());
	
	perc.jobs.init(into);
	for (auto xml_job : xml_perc.children("job")) {
		Job job;
		job.id = get_id16(xml_job.attribute("id").value());
		job.storage = get_id(xml_job.attribute("storage").value());
		narrow(job.start, xml_job.attribute("start").as_int());
		narrow(job.end, xml_job.attribute("end").as_int());
		narrow(job.reward, xml_job.attribute("reward").as_int());
		perc.jobs.push_back(job, into);
	}
	Job* jobb = perc.jobs.begin();
	for (auto xml_job : xml_perc.children("job")) {
		assert(jobb != perc.jobs.end());
		jobb->required.init(into);
		for (auto xml_item : xml_job.children("required")) {
			Item_stack item;
			item.item = get_id(xml_item.attribute("name").value());
			if (xml_item.attribute("amount").as_int() > 254) {
				item.amount = 254;
			} else {
				narrow(item.amount, xml_item.attribute("amount").as_int());
			}
			jobb->required.push_back(item, into);
		}
		++jobb;
	}
	assert(jobb == perc.jobs.end());
	
	perc.missions.init(into);
	for (auto xml_job : xml_perc.children("mission")) {
		Mission job;
		job.id = get_id16(xml_job.attribute("id").value());
		job.storage = get_id(xml_job.attribute("storage").value());
		narrow(job.start, xml_job.attribute("start").as_int());
		narrow(job.end, xml_job.attribute("end").as_int());
		narrow(job.reward, xml_job.attribute("reward").as_int());
		narrow(job.auction_time, xml_job.attribute("auctionTime").as_int());
		narrow(job.fine, xml_job.attribute("fine").as_int());
		narrow(job.max_bid, xml_job.attribute("maxBid").as_int());
		perc.missions.push_back(job, into);
	}
	Mission* jobc = perc.missions.begin();
	for (auto xml_job : xml_perc.children("mission")) {
		assert(jobc != perc.missions.end());
		jobc->required.init(into);
		for (auto xml_item : xml_job.children("required")) {
			Item_stack item;
			item.item = get_id(xml_item.attribute("name").value());
			if (xml_item.attribute("amount").as_int() > 254) {
				item.amount = 254;
			} else {
				narrow(item.amount, xml_item.attribute("amount").as_int());
			}
			jobc->required.push_back(item, into);
		}
		++jobc;
	}
	assert(jobc == perc.missions.end());

	perc.posteds.init(into);
	for (auto xml_job : xml_perc.children("posted")) {
		Posted job;
		job.id = get_id16(xml_job.attribute("id").value());
		job.storage = get_id(xml_job.attribute("storage").value());
		narrow(job.start, xml_job.attribute("start").as_int());
		narrow(job.end, xml_job.attribute("end").as_int());
		narrow(job.reward, xml_job.attribute("reward").as_int());
		perc.posteds.push_back(job, into);
	}
	Posted* jobd = perc.posteds.begin();
	for (auto xml_job : xml_perc.children("posted")) {
		assert(jobd != perc.posteds.end());
		jobd->required.init(into);
		for (auto xml_item : xml_job.children("required")) {
			Item_stack item;
			item.item = get_id(xml_item.attribute("name").value());
			if (xml_item.attribute("amount").as_int() > 254) {
				item.amount = 254;
			} else {
				narrow(item.amount, xml_item.attribute("amount").as_int());
			}
			jobd->required.push_back(item, into);
		}
		++jobd;
	}
	assert(jobd == perc.posteds.end());
		
	into->trap_alloc(false);
	assert(into->size() - prev_size == space_needed);
}

/**
 * This is a helper struct, managing the additional memory for the messages.
 */
struct Additional_block {
    int id;
    int size;
    char* data() { return (char*)(&size + 1); }
    int offset() { return sizeof(Additional_block) + size; }
};

// see header
u8 get_next_message(Socket& sock, Buffer* into) {
	assert(into);

    memory_for_messages.reset();

    // There may be data left from the last invocation, append it.
    int id = sock.get_id();
    for (int i = 0; i < additional_data.size();) {
        auto& block = additional_data.get<Additional_block>(i);
        if (block.id == id) {
            memory_for_messages.append(block.data(), block.size);
            int offset = block.offset();
            char* next = ((char*)&block) + offset;
            std::memmove((char*)&block, next, additional_data.end() - next);
            additional_data.addsize(-offset);
        } else {
            i += block.offset();
        }
    }
    
	// Make the buffer bigger if needed. This should not happen, but I don't
	// like having no fallbacks.
	if (additional_buffer_needed) {
		memory_for_messages.reserve_space(additional_buffer_needed);
		additional_buffer_needed = 0;
	}
    memory_for_messages.trap_alloc(true);

    int prev_size = 0;
	while (true) {
        // The message is complete if there is a terminating zero, but there may
        // be data behind that. This data will be collected in additional_data.
        bool zero_found = false;
        for (int i = prev_size; i < memory_for_messages.size(); ++i) {
            if (memory_for_messages[i] == 0) {
                int size = memory_for_messages.size() - i - 1;
                if (size) {
                    additional_data.emplace_back<Additional_block>(id, size);
                    additional_data.append({memory_for_messages.data() + i+1, size});
                    memory_for_messages.resize(i + 1);
                }
                zero_found = true;
                break;
            }
        }
        if (zero_found) break;
        
        prev_size = memory_for_messages.size();
		sock.recv(&memory_for_messages);
		assert(memory_for_messages.size() > prev_size);
	}

    if (dump_xml_output) {
        *dump_xml_output << "<<< incoming <<<\n" << memory_for_messages.data() << '\n';
    }
    {
        pugi::xml_document doc;
        assert(doc.load_buffer_inplace(memory_for_messages.data(), memory_for_messages.size()));

        auto xml_mess = doc.child("message");
        auto type = xml_mess.attribute("type").value();

        int prev_size = into->size();
	
        if (std::strcmp(type, "auth-response") == 0) {
            parse_auth_response(xml_mess.child("auth-response"), into);
        } else if (std::strcmp(type, "sim-start") == 0) {
            parse_sim_start(xml_mess.child("simulation"), into);
        } else if (std::strcmp(type, "sim-end") == 0) {
            parse_sim_end(xml_mess.child("sim-result"), into);
        } else if (std::strcmp(type, "request-action") == 0) {
            parse_request_action(xml_mess.child("percept"), into);
        } else if (std::strcmp(type, "bye") == 0) {
            into->emplace_back<Message_Bye>();
        } else {
            assert(false);
        }

        auto& mess = into->get<Message_Server2Client>(prev_size);
        narrow(mess.timestamp, xml_mess.attribute("timestamp").as_ullong());
        memory_for_messages.trap_alloc(false);
        return mess.type;
    }
}

// Helper for the send_message functions
pugi::xml_node prep_message_xml(Message const& mess, pugi::xml_document* into,
								char const* type) {
	assert(into);
	auto xml_decl = into->prepend_child(pugi::node_declaration);
	xml_decl.append_attribute("version") = "1.0";
	xml_decl.append_attribute("encoding") = "UTF-8";
	xml_decl.append_attribute("standalone") = "no";

	auto xml_mess = into->append_child("message");
	xml_mess.append_attribute("type") = type;
	return xml_mess;
}
void send_xml_message(Socket& sock, pugi::xml_document& doc) {
	Socket_writer writer {&sock};
	doc.save(writer, "", pugi::format_default, pugi::encoding_utf8);
    if (dump_xml_output) {
        *dump_xml_output << ">>> outgoing >>>\n";
        doc.save(*dump_xml_output);
        *dump_xml_output << '\n';
    }
	sock.send({"", 1});
}

// see header
void send_message(Socket& sock, Message_Auth_Request const& mess) {
	pugi::xml_document doc;
	auto xml_mess = prep_message_xml(mess, &doc, "auth-request");
	
	auto xml_auth = xml_mess.append_child("auth-request");
	xml_auth.append_attribute("username") = mess.username.c_str();
	xml_auth.append_attribute("password") = mess.password.c_str();
	
	send_xml_message(sock, doc);
}
#if 0
/**
 * Helper, generates the parameter for the Action message.
 */
char const* generate_action_param(Action const& action) {
	pugi::xml_document doc;
	auto param = doc.append_child("param");

	// Helper, write a list of Item_stack to param in the form
	//   item1="..." item2="..." ... amount1="..." ...
	auto write_item_stack_list = [&param](Flat_array<Item_stack> const& items) {
		constexpr static int buf_len = 16;
		constexpr static char const* str1("item");
		constexpr static char const* str2("amount");
			
		memory_for_messages.reserve_space(
			buf_len + std::max(strlen(str1), strlen(str2))
		);
		char* buf = memory_for_messages.end();
		std::strcpy(buf, str1);
		for (int i = 0; i < items.size(); ++i) {
			std::snprintf(buf + strlen(str1), buf_len, "%d", i + 1);
			param.append_attribute(buf) = get_string_from_id(items[i].item).c_str();
		}
		std::strcpy(buf, str2);
		for (int i = 0; i < items.size(); ++i) {
			std::snprintf(buf + strlen(str2), buf_len, "%d", i + 1);
			param.append_attribute(buf) = items[i].amount;
		}
	};

	switch (action.type) {
	case Action::GOTO: assert(false); break;
	case Action::GOTO1: {
		auto const& a = (Action_Goto1 const&) action;
		param.append_attribute("facility") = get_string_from_id(a.facility).c_str();
		break;
	}
	case Action::GOTO2: {
		auto const& a = (Action_Goto2 const&) action;
		set_xml_pos(a.pos, &param);
		break;
	}
	case Action::BUY: {
		auto const& a = (Action_Buy const&) action;
		param.append_attribute("item") = get_string_from_id(a.item.item).c_str();
		param.append_attribute("amount") = a.item.amount;
		break;
	}
	case Action::GIVE: {
		auto const& a = (Action_Give const&) action;
		param.append_attribute("agent") = get_string_from_id(a.agent).c_str();
		param.append_attribute("item") = get_string_from_id(a.item.item).c_str();
		param.append_attribute("amount") = a.item.amount;
		break;
	}
	case Action::RECIEVE: {
		break;
	}
	case Action::STORE: {
		auto const& a = (Action_Store const&) action;
		param.append_attribute("item") = get_string_from_id(a.item.item).c_str();
		param.append_attribute("amount") = a.item.amount;
		break;
	}
	case Action::RETRIEVE: {
		auto const& a = (Action_Retrieve const&) action;
		param.append_attribute("item") = get_string_from_id(a.item.item).c_str();
		param.append_attribute("amount") = a.item.amount;
		break;
	}
	case Action::RETRIEVE_DELIVERED: {
		auto const& a = (Action_Retrieve_delivered const&) action;
		param.append_attribute("item") = get_string_from_id(a.item.item).c_str();
		param.append_attribute("amount") = a.item.amount;
		break;
	}
	case Action::DUMP: {
		auto const& a = (Action_Dump const&) action;
		param.append_attribute("item") = get_string_from_id(a.item.item).c_str();
		param.append_attribute("amount") = a.item.amount;
		break;
	}
	case Action::ASSEMBLE: {
		auto const& a = (Action_Assemble const&) action;
		param.append_attribute("item") = get_string_from_id(a.item).c_str();
		break;
	}
	case Action::ASSIST_ASSEMBLE: {
		auto const& a = (Action_Assist_assemble const&) action;
		param.append_attribute("assembler") = get_string_from_id(a.assembler).c_str();
		break;
	}
	case Action::DELIVER_JOB: {
		auto const& a = (Action_Deliver_job const&) action;
		param.append_attribute("job") = get_string_from_id(a.job).c_str();
		break;
	}
	case Action::CHARGE: {
		break;
	}
	case Action::BID_FOR_JOB: {
		auto const& a = (Action_Bid_for_job const&) action;
		param.append_attribute("job") = get_string_from_id(a.job).c_str();
		param.append_attribute("price") = a.price;
		break;
	}
	case Action::POST_JOB1: {
		auto const& a = (Action_Post_job1 const&) action;
		param.append_attribute("type") = "auction";
		param.append_attribute("max_price") = a.max_price;
		param.append_attribute("fine") = a.fine;
		param.append_attribute("active_steps") = a.active_steps;
		param.append_attribute("auction_steps") = a.auction_steps;
		param.append_attribute("storage") = get_string_from_id(a.storage).c_str();
		write_item_stack_list(a.items);
		break;
	}
	case Action::POST_JOB2: {
		auto const& a = (Action_Post_job2 const&) action;
		param.append_attribute("type") = "priced";
		param.append_attribute("price") = a.price;
		param.append_attribute("active_steps") = a.active_steps;
		param.append_attribute("storage") = get_string_from_id(a.storage).c_str();
		write_item_stack_list(a.items);
		break;
	}
	case Action::CALL_BREAKDOWN_SERVICE: {
		break;
	}
	case Action::CONTINUE: {
		break;
	}
	case Action::SKIP: {
		break;
	}
	case Action::ABORT: {
		break;
	}
	default: assert(false); break;
	}

	// Takes the xml in param, writes it, and cuts the "<param " and the " />"
	// off. If there is a better way to do this, please do!
	char* start = memory_for_messages.end();
	Buffer_writer writer {&memory_for_messages};
	param.print(writer, "", pugi::format_raw);
	memory_for_messages.emplace_back<char>('\0');
	char* end = memory_for_messages.end();	  
	assert(end[-1] == 0);
	static constexpr char const* str1 = "<param ";
	assert(strncmp(start, str1, strlen(str1)) == 0);
	start += strlen(str1);
	end -= strlen("/>") + 1;
	assert(strcmp(end, "/>") == 0);
	*end = 0;

    {
        char* d = start;
        char* p = start;
        while (p != end) {
            while (*p == '"') ++p;
            *d++ = *p++;
        }
        *d = 0;
    }
	return start;
}
#endif
// see header
void send_message(Socket& sock, Message_Action const& mess) {
    memory_for_messages.trap_alloc(true);
    {
        pugi::xml_document doc;    
        auto xml_mess = prep_message_xml(mess, &doc, "action");
	
        auto xml_auth = xml_mess.append_child("action");
        xml_auth.append_attribute("id") = mess.id;
        xml_auth.append_attribute("type") = Action::get_name(mess.action->type);


		Action const& action = *mess.action;

#define next_arg xml_auth.append_child("p").text()
#define cast(T) auto const& a = (T const&) action

		switch (mess.action->type) {
		case Action::GIVE: {
			cast(Action_Give);
			next_arg = a.agent;
			next_arg = a.item.item;
			next_arg = a.item.amount;
		} break;
		case Action::STORE:
		case Action::RETRIEVE:
		case Action::RETRIEVE_DELIVERED:
		case Action::BUY:
		case Action::DUMP: {
			cast(Action_Store);
			next_arg = a.item.item;
			next_arg = a.item.amount;
		} break;
		case Action::ASSEMBLE:
		case Action::ASSIST_ASSEMBLE:
		case Action::GOTO1: {
			cast(Action_Assemble);
			next_arg = get_string_from_id(a.item);
		} break;
		case Action::DELIVER_JOB: {
			cast(Action_Deliver_job);
			next_arg = get_string_from_id(a.job);
		} break;
		case Action::BID_FOR_JOB: {
			cast(Action_Bid_for_job);
			next_arg = a.job;
			next_arg = a.bid;
		} break;
		case Action::POST_JOB: {
			cast(Action_Post_job);
			next_arg = a.reward;
			next_arg = a.duration;
			next_arg = a.storage;
			for (Item_stack i : a.items) {
				next_arg = i.item;
				next_arg = i.amount;
			}
		} break;
		case Action::GOTO2: {
			cast(Action_Goto2);
			auto const back = get_pos_back(a.pos);
			next_arg = back.first;
			next_arg = back.second;
		} break;
		case Action::RECIEVE:
		case Action::CHARGE:
		case Action::RECHARGE:
		case Action::CONTINUE:
		case Action::SKIP:
		case Action::GATHER:
		case Action::GOTO0:
			break;
		case Action::GOTO:
		case Action::UNKNOWN_ACTION:
		case Action::RANDOM_FAIL:
		case Action::NO_ACTION:
		default:
			assert(false);
		}
#undef next_arg
#undef cast
        send_xml_message(sock, doc);
    }
    memory_for_messages.trap_alloc(false);
    memory_for_messages.reset();
}

} /* end of namespace jup */

int messages_main() {
	jup::Socket_context context;
	jup::Socket sock {"localhost", "12300"};
	if (!sock) { return 1; }
	
	jup::init_messages();

	jup::send_message(sock, jup::Message_Auth_Request {"a1", "1"});

	jup::Buffer buffer;
	get_next_message(sock, &buffer);
	auto& mess1 = buffer.get<jup::Message_Auth_Response>();
	if (mess1.succeeded) {
		jup::jout << "Conected to server. Please start the simulation.\n";
		jup::jout.flush();
	} else {
		jup::jout << "Invalid authentification.\n";
		return 1;
	}

	buffer.reset();
	get_next_message(sock, &buffer);
	auto& mess2 = buffer.get<jup::Message_Sim_Start>();
	jup::jout << "Got the simulation. Steps: "
			  << mess2.simulation.steps << '\n';

	while (true) {
		buffer.reset();
		assert(get_next_message(sock, &buffer) == jup::Message::REQUEST_ACTION);
		auto& mess = buffer.get<jup::Message_Request_Action>();
		jup::jout << "Got the message request. Step: "
				  << mess.perception.simulation_step << '\n';

		auto& answ = jup::memory_for_messages.emplace_back<jup::Message_Action>
			( mess.perception.id, jup::Action_Skip {}, &jup::memory_for_messages );
		jup::send_message(sock, answ);
	}
	
    return 0;
}

