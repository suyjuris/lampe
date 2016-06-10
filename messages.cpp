
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

// Data for mapping coordinates to u8
constexpr double mess_lat_lon_padding = 0.2;
bool messages_lat_lon_initialized = false;
double mess_min_lat;
double mess_max_lat;
double mess_min_lon;
double mess_max_lon;
float mess_scale_lat;
float mess_scale_lon;

/**
 * Parse a point and change the min/max lat/lon accordingly. This assumes that
 * we are in an area that does not have wrapping longitudes (like 179°´to -179°)
 * and is nearly planar.
 */
void add_bound_point(pugi::xml_node xml_obj) {
	double lat = xml_obj.attribute("lat").as_double();
	double lon = xml_obj.attribute("lon").as_double();
	if (!messages_lat_lon_initialized) {
		mess_min_lat = lat;
		mess_max_lat = lat;
		mess_min_lon = lon;
		mess_max_lon = lon;
		messages_lat_lon_initialized = true;
	}
	if (lat < mess_min_lat) mess_min_lat = lat;
	if (lat > mess_max_lat) mess_max_lat = lat;
	if (lon < mess_min_lon) mess_min_lon = lon;
	if (lon > mess_max_lon) mess_max_lon = lon;
    
    float lon_radius = std::cos((mess_max_lat + mess_min_lat) / 360. * M_PI) * radius_earth;
    auto pmin = get_pos_back({0, 0});
    auto pmax = get_pos_back({255, 255});
    mess_scale_lat = (pmax.first  - pmin.first ) / 180.f * (radius_earth * M_PI) / 255.f;
    mess_scale_lon = (pmax.second - pmin.second) / 180.f * (lon_radius   * M_PI) / 255.f;
}

/**
 * Construct the Pos object from the coordinates in xml_obj
 */
Pos get_pos(pugi::xml_node xml_obj) {
	constexpr static double pad = mess_lat_lon_padding;
	double lat = xml_obj.attribute("lat").as_double();
	double lon = xml_obj.attribute("lon").as_double();
	double lat_diff = (mess_max_lat - mess_min_lat);
	double lon_diff = (mess_max_lon - mess_min_lon);
	lat = (lat - mess_min_lat + lat_diff * pad) / (1 + 2*pad) / lat_diff;
	lon = (lon - mess_min_lon + lon_diff * pad) / (1 + 2*pad) / lon_diff;
	assert(0.0 <= lat and lat < 1.0);
	assert(0.0 <= lon and lon < 1.0);
	return Pos {(u8)(lat * 256.0), (u8)(lon * 256.0)};
}

std::pair<double, double> get_pos_back(Pos pos) {
    constexpr static double pad = mess_lat_lon_padding;
	double lat_diff = (mess_max_lat - mess_min_lat);
	double lon_diff = (mess_max_lon - mess_min_lon);
	double lat = (double)pos.lat / 256.0;
	double lon = (double)pos.lon / 256.0;
	lat = lat * lat_diff * (1 + 2*pad) - lat_diff * pad + mess_min_lat;
	lon = lon * lon_diff * (1 + 2*pad) - lon_diff * pad + mess_min_lon;
    return {lat, lon};
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
		space_needed += s + sizeof(u8) * distance(xml_obj.child("role"));
		space_needed += s;
		for (auto xml_prod: xml_obj.child("products")) {
			space_needed += s * 2 + sizeof(Product)
				+ sizeof(Item_stack) * distance(xml_prod.child("consumed"))
				+ sizeof(Item_stack) * distance(xml_prod.child("tools"));
		}
	}
	into->reserve_space(space_needed);
	into->trap_alloc(true);
	
	auto& sim = into->emplace_back<Message_Sim_Start>().simulation;
	narrow(sim.id,           xml_obj.attribute("id")         .as_int());
	narrow(sim.seed_capital, xml_obj.attribute("seedCapital").as_int());
	narrow(sim.steps,        xml_obj.attribute("steps")      .as_int());
	sim.team = get_id(xml_obj.attribute("team").value());

	auto xml_role = xml_obj.child("role");
	sim.role.name = get_id(xml_role.attribute("name").value());
	narrow(sim.role.speed,       xml_role.attribute("speed")     .as_int());
	narrow(sim.role.max_battery, xml_role.attribute("maxBattery").as_int());
	narrow(sim.role.max_load,    xml_role.attribute("maxLoad")   .as_int());
	sim.role.tools.init(into);
	for (auto xml_tool: xml_role.children("tool")) {
		u8 name = get_id(xml_tool.attribute("name").value());
		sim.role.tools.push_back(name, into);
	}

	sim.products.init(into);
	for (auto xml_prod: xml_obj.child("products").children("product")) {
		Product prod;
		prod.name = get_id(xml_prod.attribute("name").value());
		prod.assembled = xml_prod.attribute("assembled").as_bool();
		narrow(prod.volume, xml_prod.attribute("volume").as_int());
		sim.products.push_back(prod, into);
	}
	Product* prod = sim.products.begin();
	for (auto xml_prod: xml_obj.child("products").children("product")) {
		assert(prod != sim.products.end());
		prod->consumed.init(into);
		if (auto xml_cons = xml_prod.child("consumed")) {
			for (auto xml_item: xml_cons.children("item")) {
				Item_stack stack;
				stack.item = get_id(xml_item.attribute("name").value());
				narrow(stack.amount, xml_item.attribute("amount").as_int());
				prod->consumed.push_back(stack, into);
			}
		}
		prod->tools.init(into);
		if (auto xml_tools = xml_prod.child("tools")) {
			for (auto xml_tool: xml_tools.children("item")) {
				// TODO this may actually not hold
				//u8 id = get_id(xml_tool.attribute("name").value());
                //assert(xml_tool.attribute("amount").as_int() == 1);
				Item_stack stack;
				stack.item = get_id(xml_tool.attribute("name").value());
				narrow(stack.amount, xml_tool.attribute("amount").as_int());
				prod->tools.push_back(stack, into);
			}
		}
		++prod;
	}
	assert(prod == sim.products.end());
	
	into->trap_alloc(false);
	assert(into->size() - prev_size ==  space_needed);
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
	auto xml_team = xml_perc.child("team");

	int prev_size = into->size();
	int space_needed = sizeof(Message_Request_Action);
	{
		constexpr int s = sizeof(u8);
		space_needed += s + sizeof(Item_stack) * distance(xml_self.child("items"));
		space_needed += s + sizeof(Pos) * distance(xml_self.child("route"));
		space_needed += s + sizeof(u8) * distance(xml_team.child("jobs-taken"));
		space_needed += s + sizeof(u8) * distance(xml_team.child("jobs-posted"));
		space_needed += s + sizeof(Entity) * distance(xml_perc.child("entities"));

        auto xml_facs = xml_perc.child("facilities");
		space_needed += 5 * s
            + sizeof(Charging_station) * distance(xml_facs.children("chargingStation"))
            + sizeof(Dump_location)    * distance(xml_facs.children("dumpLocation"))
            + sizeof(Shop)             * distance(xml_facs.children("shop"))
            + sizeof(Storage)          * distance(xml_facs.children("storage"))
            + sizeof(Workshop)         * distance(xml_facs.children("workshop"));
        
		for (auto xml_fac : xml_facs.children("shop")) {
			space_needed += s + sizeof(Shop_item) * distance(xml_fac.children("item"));
		}
		for (auto xml_fac : xml_facs.children("storage")) {
			space_needed += s + sizeof(Storage_item) * distance(xml_fac.children("item"));
		}

		space_needed += 2 * s;
		for (auto xml_job: xml_perc.child("jobs").children("auctionJob")) {
			space_needed += sizeof(Job_auction) + s
				+ sizeof(Job_item) * distance(xml_job.child("items"));
		}
		for (auto xml_job: xml_perc.child("jobs").children("pricedJob")) {
			space_needed += sizeof(Job_priced) + s
				+ sizeof(Job_item) * distance(xml_job.child("items"));
		}
	}
	
	into->reserve_space(space_needed);
	into->trap_alloc(true);

	auto& perc = into->emplace_back<Message_Request_Action>().perception;

	if (!messages_lat_lon_initialized) {
		add_bound_point(xml_self);
		for (auto i: xml_perc.child("facilities")) {
			add_bound_point(i);
		}
		for (auto i: xml_perc.child("entities")) {
			add_bound_point(i);
		}
	}

	narrow(perc.deadline, xml_perc.attribute("deadline").as_ullong());
	narrow(perc.id,       xml_perc.attribute("id")      .as_int());
	narrow(perc.simulation_step,
		   xml_perc.child("simulation").attribute("step").as_int());
	
	auto& self = perc.self;
	narrow(self.charge,     xml_self.attribute("charge")   .as_int());
	narrow(self.load,       xml_self.attribute("load")     .as_int());
	self.last_action = Action::get_id(xml_self.attribute("lastAction").value());
	self.last_action_result = Action::get_result_id(
		xml_self.attribute("lastActionResult").value());
	self.pos = get_pos(xml_self);
	char const* in_fac = xml_self.attribute("inFacility").value();
	if (std::strcmp(in_fac, "none") == 0) {
		self.in_facility = 0;
	} else {		
		self.in_facility = get_id(in_fac);
	}
	int fpos = xml_self.attribute("fPosition").as_int();
	if (fpos == -1) {
		self.f_position = -1;
	} else {
		narrow(self.f_position, fpos);
	}
	
	self.items.init(into);
	for (auto xml_item: xml_self.child("items").children("item")) {
		Item_stack item;
		item.item = get_id(xml_item.attribute("name").value());
		narrow(item.amount, xml_item.attribute("amount").as_int());
		self.items.push_back(item, into);
	}
	self.route.init(into);
	for (auto xml_node: xml_self.child("route").children("n")) {
		self.route.push_back(get_pos(xml_node), into);
	}

	auto& team = perc.team;

	team.jobs_taken.init(into);
	for (auto xml_job: xml_team.child("jobs-taken").children("job")) {
		u16 job_id = get_id(xml_job.attribute("id").value());
		team.jobs_taken.push_back(job_id, into);
	}
	team.jobs_posted.init(into);
	for (auto xml_job: xml_team.child("jobs-posted").children("job")) {
		u16 job_id = get_id(xml_job.attribute("id").value());
		team.jobs_posted.push_back(job_id, into);
	}

	perc.entities.init(into);
	for (auto xml_ent: xml_perc.child("entities").children("entity")) {
		Entity ent;
		ent.name = get_id(xml_ent.attribute("name").value());
		ent.team = get_id(xml_ent.attribute("team").value());
		ent.pos =  get_pos(xml_ent);
		ent.role = get_id(xml_ent.attribute("role").value());
		perc.entities.push_back(ent, into);
	}
	
	perc.charging_stations.init(into);
	for (auto xml_fac: xml_perc.child("facilities").children("chargingStation")) {
		Charging_station fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		narrow(fac.rate,  xml_fac.attribute("rate") .as_int());
		narrow(fac.price, xml_fac.attribute("price").as_int());
		narrow(fac.slots, xml_fac.attribute("slots").as_int());
		if (xml_fac.child("info")) {
			narrow(fac.q_size, xml_fac.child("info").attribute("qSize").as_int());
			assert(fac.q_size + 1);
		} else {
			fac.q_size = -1;
		}
		perc.charging_stations.push_back(fac, into);
	}
	perc.dump_locations.init(into);
	for (auto xml_fac: xml_perc.child("facilities").children("dumpLocation")) {
		Dump_location fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		narrow(fac.price, xml_fac.attribute("price").as_int());
		perc.dump_locations.push_back(fac, into);
	}
    
	perc.shops.init(into);
	for (auto xml_fac: xml_perc.child("facilities").children("shop")) {
		Shop fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		perc.shops.push_back(fac, into);
	}	
	Shop* shop = perc.shops.begin();
	for (auto xml_fac : xml_perc.child("facilities").children("shop")) {
		assert(shop != perc.shops.end());
		shop->items.init(into);
		for (auto xml_item : xml_fac.children("item")) {
			Shop_item item;
			item.item = get_id(xml_item.attribute("name").value());
            if (auto xml_info = xml_item.child("info")) {
                if (xml_info.attribute("amount").as_int() > 254) {
                    item.amount = 254;
                } else {
                    narrow(item.amount, xml_info.attribute("amount").as_int());
                }
                narrow(item.cost, xml_info.attribute("cost").as_int());
                narrow(item.restock, xml_info.attribute("restock").as_int());
            } else {
                item.amount = 0xff;
                item.cost = 0;
                item.restock = 0;
            }
			shop->items.push_back(item, into);
		}
		++shop;
	}
	assert(shop == perc.shops.end());
    
	perc.storages.init(into);
	for (auto xml_fac: xml_perc.child("facilities").children("storage")) {
		Storage fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		perc.storages.push_back(fac, into);
	}
	Storage* storage = perc.storages.begin();
	for (auto xml_fac : xml_perc.child("facilities").children("storage")) {
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
	for (auto xml_fac: xml_perc.child("facilities").children("workshop")) {
		Workshop fac;
		fac.name = get_id(xml_fac.attribute("name").value());
		fac.pos = get_pos(xml_fac);
		perc.workshops.push_back(fac, into);
	}

	perc.auction_jobs.init(into);
	for (auto xml_job: xml_perc.child("jobs").children("auctionJob")) {
		Job_auction job;
		job.id      = get_id16(xml_job.attribute("id")     .value());
		job.storage = get_id  (xml_job.attribute("storage").value());
		narrow(job.begin,   xml_job.attribute("begin") .as_int());
		narrow(job.end,     xml_job.attribute("end")   .as_int());
		narrow(job.fine,    xml_job.attribute("fine")  .as_int());
		narrow(job.max_bid, xml_job.attribute("maxBid").as_int());
		perc.auction_jobs.push_back(job, into);
	}
	Job_auction* joba = perc.auction_jobs.begin();
	for (auto xml_job: xml_perc.child("jobs").children("auctionJob")) {
		assert(joba != perc.auction_jobs.end());
		joba->items.init(into);
		for (auto xml_item: xml_job.child("items").children("item")) {
			Job_item item;
			item.item = get_id(xml_item.attribute("name").value());
			narrow(item.amount,    xml_item.attribute("amount")   .as_int());
			narrow(item.delivered, xml_item.attribute("delivered").as_int());
			joba->items.push_back(item, into);
		}
		++joba;
	}
	assert(joba == perc.auction_jobs.end());
	
	perc.priced_jobs.init(into);
	for (auto xml_job: xml_perc.child("jobs").children("pricedJob")) {
		Job_priced job;
		job.id      = get_id16(xml_job.attribute("id")     .value());
		job.storage = get_id  (xml_job.attribute("storage").value());
		narrow(job.begin,   xml_job.attribute("begin") .as_int());
		narrow(job.end,     xml_job.attribute("end")   .as_int());
		narrow(job.reward,  xml_job.attribute("reward").as_int());
		perc.priced_jobs.push_back(job, into);
	}
	Job_priced* jobp = perc.priced_jobs.begin();
	for (auto xml_job: xml_perc.child("jobs").children("pricedJob")) {
		assert(jobp != perc.priced_jobs.end());
		jobp->items.init(into);
		for (auto xml_item: xml_job.child("items").children("item")) {
			Job_item item;
			item.item = get_id(xml_item.attribute("name").value());
			narrow(item.amount,    xml_item.attribute("amount")   .as_int());
			narrow(item.delivered, xml_item.attribute("delivered").as_int());
			jobp->items.push_back(item, into);
		}
		++jobp;
	}
	assert(jobp == perc.priced_jobs.end());
		
	into->trap_alloc(false);
	assert(into->size() - prev_size == space_needed);
}

// see header
u8 get_next_message(Socket& sock, Buffer* into) {
	assert(into);

	memory_for_messages.reset();

	// Make the buffer bigger if needed. This should not happen, but I don't
	// like having no fallbacks.
	if (additional_buffer_needed) {
		memory_for_messages.reserve_space(additional_buffer_needed);
		additional_buffer_needed = 0;
	}
    memory_for_messages.trap_alloc(true);

	do {
		sock.recv(&memory_for_messages);
		assert(memory_for_messages.size());
	} while (memory_for_messages.end()[-1] != 0);

    if (dump_xml_output) {
        *dump_xml_output << "<<< incoming <<<\n" << memory_for_messages.data() << '\n';
    }
    
	pugi::xml_document doc;
	assert(doc.load_buffer_inplace(memory_for_messages.data(), memory_for_messages.size()));

	auto xml_mess = doc.child("message");
	auto type = xml_mess.attribute("type").value();

	int prev_size = into->size();
	
	if (std::strcmp(type, "auth-response") == 0) {
		parse_auth_response(xml_mess.child("authentication"), into);
	} else if (std::strcmp(type, "sim-start") == 0) {
		parse_sim_start(xml_mess.child("simulation"), into);
	} else if (std::strcmp(type, "sim-end") == 0) {
		parse_sim_end(xml_mess.child("sim-result"), into);
	} else if (std::strcmp(type, "request-action") == 0) {
		parse_request_action(xml_mess.child("perception"), into);
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

// Helper for the send_message functions
pugi::xml_node prep_message_xml(Message const& mess, pugi::xml_document* into,
								char const* type) {
	assert(into);
	auto xml_decl = into->prepend_child(pugi::node_declaration);
	xml_decl.append_attribute("version") = "1.0";
	xml_decl.append_attribute("encoding") = "UTF-8";

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
	
	auto xml_auth = xml_mess.append_child("authentication");
	xml_auth.append_attribute("username") = mess.username.c_str();
	xml_auth.append_attribute("password") = mess.password.c_str();
	
	send_xml_message(sock, doc);
}

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

// see header
void send_message(Socket& sock, Message_Action const& mess) {
    // Do it up here, so that pugi has no problems with its memory
    auto action_param = generate_action_param(*mess.action);
    memory_for_messages.trap_alloc(true);
    {
        pugi::xml_document doc;    
        auto xml_mess = prep_message_xml(mess, &doc, "action");
	
        auto xml_auth = xml_mess.append_child("action");
        xml_auth.append_attribute("id") = mess.id;
        xml_auth.append_attribute("type") = Action::get_name(mess.action->type);
        xml_auth.append_attribute("param") = action_param;
	
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

