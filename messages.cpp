

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>

#include "messages.hpp"
#include "pugixml.hpp"

namespace jup {

template <typename Range>
auto distance(Range const& range) {
	return std::distance(range.begin(), range.end());
}

struct Socket_writer: public pugi::xml_writer {
	Socket_writer(Socket* sock): sock{sock} {}

	void write(const void* data, std::size_t size) override {
		sock->send(Buffer_view {data, size});
	}

	Socket* sock;
};

static Buffer memory_for_messages;
static int additional_buffer_needed = 0;

static Buffer memory_for_strings;

void* allocate_pugi(size_t size) {
	constexpr static int align = alignof(double) > alignof(void*)
		? alignof(double) : alignof(void*);
	void* result = memory_for_messages.end();
	std::size_t space = memory_for_messages.space();
	if (std::align(align, size, result, space)) {
		memory_for_messages.addsize((char*)result + size - memory_for_messages.begin());
		return result;
	} else {
		jerr << "Warning: Buffer for pugi is not big enough\n";
		additional_buffer_needed += size;
		return malloc(size);
	}
}

void deallocate_pugi(void* ptr) {
	if (memory_for_messages.begin() > ptr or ptr >= memory_for_messages.end()) {
		free(ptr);
	}
}

void init_messages() {
	pugi::set_memory_management_functions(&allocate_pugi, &deallocate_pugi);
	memory_for_messages.reserve(65536);

	memory_for_strings.reserve(2048);
	memory_for_strings.emplace_ref<Flat_idmap>();
}

u8 get_id(Buffer_view str) {
	auto& map = memory_for_strings.get_ref<Flat_idmap>();
	return map.get_id(str, &memory_for_strings);
}

u8 get_id_from_string(Buffer_view str) {
	return memory_for_strings.get_ref<Flat_idmap>().get_id(str);
}

Buffer_view get_string_from_id(u8 id) {
	auto& map = memory_for_strings.get_ref<Flat_idmap>();
	return map.get_value(id);
}

template <typename T, typename R>
void narrow(T& into, R from) {
	into = static_cast<T>(from);
	assert(into == from and (into > 0) == (from > 0));
}

void parse_auth_response(pugi::xml_node xml_obj, Buffer* into) {
	assert(into);
	auto& mess = into->emplace_ref<Message_Auth_Response>();
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

	int space_needed = sizeof(Message_Sim_Start);
	space_needed += sizeof(u16)
		+ sizeof(u8) * distance(xml_obj.child("role"));
	for (auto xml_prod: xml_obj.child("products")) {
		space_needed += sizeof(Product)
			+ sizeof(u16) * 2
			+ sizeof(Item_stack) * distance(xml_prod.child("consumed"))
			+ sizeof(u8) * distance(xml_prod.child("tools"));
	}
	into->reserve_space(space_needed);
	into->trap_alloc(true);
	
	auto& sim = into->emplace_ref<Message_Sim_Start>().simulation;
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
		if (auto xml_cons = xml_prod.child("consumed")) {
			prod->consumed.init(into);
			for (auto xml_item: xml_cons.children("item")) {
				Item_stack stack;
				stack.item = get_id(xml_item.attribute("name").value());
				narrow(stack.amount, xml_item.attribute("amount").as_int());
				prod->consumed.push_back(stack, into);
			}
		}
		if (auto xml_cons = xml_prod.child("tools")) {
			prod->tools.init(into);
			for (auto xml_tool: xml_cons.children("tool")) {
				u8 id = get_id(xml_tool.attribute("name").value());
				prod->tools.push_back(id, into);
			}
		}
		++prod;
	}
	
	into->trap_alloc(false);
}

void get_next_message(Socket& sock, Buffer* into) {
	assert(into);

	memory_for_messages.reset();
	memory_for_messages.reserve(additional_buffer_needed + memory_for_messages.capacity());
	additional_buffer_needed = 0;

	do {
		sock.recv(&memory_for_messages);
		assert(memory_for_messages.size());
	} while (memory_for_messages.end()[-1] != 0);
		
	pugi::xml_document doc;
	assert(doc.load_buffer_inplace(memory_for_messages.data(), memory_for_messages.size()));
	auto xml_mess = doc.child("message");

	auto type = xml_mess.attribute("type").value();
	
	if (std::strcmp(type, "auth-response") == 0) {
		parse_auth_response(xml_mess.child("authentication"), into);
	} else if (std::strcmp(type, "sim-start") == 0) {
		parse_sim_start(xml_mess.child("simulation"), into);
	} else {
		assert(false);
	}
	
	auto& mess = into->get_ref<Message_Server2Client>();
	narrow(mess.timestamp, xml_mess.attribute("timestamp").as_ullong());
}

pugi::xml_node prep_message_xml(Message const& mess, pugi::xml_document* into) {
	assert(into);
	auto xml_decl = into->prepend_child(pugi::node_declaration);
	xml_decl.append_attribute("version") = "1.0";
	xml_decl.append_attribute("encoding") = "UTF-8";

	auto xml_mess = into->append_child("message");
	xml_mess.append_attribute("type") = "auth-request";
	return xml_mess;
}
void send_xml_message(Socket& sock, pugi::xml_document& doc) {
	Socket_writer writer {&sock};
	doc.save(writer, "", pugi::format_default, pugi::encoding_utf8);
	sock.send({"", 1});
}

void send_message(Socket& sock, Message_Auth_Request const& mess) {
	pugi::xml_document doc;
	auto xml_mess = prep_message_xml(mess, &doc);
	
	auto xml_auth = xml_mess.append_child("authentication");
	xml_auth.append_attribute("username") = mess.username.c_str();
	xml_auth.append_attribute("password") = mess.password.c_str();
	
	send_xml_message(sock, doc);
}

} /* end of namespace jup */

int main() {
	jup::Socket_context context;
	jup::Socket sock {"localhost", "12300"};
	if (!sock) { return 1; }
	
	jup::init_messages();

	jup::send_message(sock, jup::Message_Auth_Request {"a1", "1"});

	jup::Buffer buffer;
	get_next_message(sock, &buffer);
	auto& mess = buffer.get_ref<jup::Message_Auth_Response>();
	if (mess.succeeded) {
		jup::jout << "Conected to server. Please start the simulation.\n";
		jup::jout.flush();
	} else {
		jup::jout << "Invalid authentification.\n";
		return 1;
	}

	buffer.reset();
	get_next_message(sock, &buffer);
	auto& mess2 = buffer.get_ref<jup::Message_Sim_Start>();
	jup::jout << "Actually got the simulation. Total number of steps: ";
	jup::jout << mess2.simulation.steps << '\n';
	
    return 0;
}

