
#include "agent.hpp"

#include <cmath>

#include "buffer.hpp"

namespace jup {

Action const& dummy_agent (u8 id, Simulation const& sim, Perception const& perc) {
    static Buffer buffer;
    return buffer.emplace<Action_Skip>();
}

u8 find_item_source(u8 item, Simulation const& sim, Perception const& perc) {
    for (Storage const& i: perc.storages) {
        for (Storage_item j: i.items) {
            if (j.item == item and j.amount > j.delivered) {
                return i.name;
            }
        }
    }
    for (Shop const& i: perc.shops) {
        for (Shop_item j: i.items) {
            if (j.item == item) {
                return i.name;
            }
        }
    }
    for (Product const& i: sim.products) {
        if (i.name == item and i.assembled) {
            for (u8 j: i.tools) {
                bool flag = false;
                for (Item_stack k: perc.self.items) {
                    if (k.item == j) {
                        flag = true;
                        break;
                    }
                }
                if (!flag) return 0;
            }
            for (Item_stack j: i.consumed) {
                bool flag = false;
                for (Item_stack k: perc.self.items) {
                    if (k.item == j.item and k.amount >= j.amount) {
                        flag = true;
                        break;
                    }
                }
                if (!flag) {
                    return find_item_source(j.item, sim, perc);
                }
            }
        }
    }
    
    return 0;
}
Job_priced const* find_job(u8 name, Perception const& perc) {
    for (Job_priced const& job: perc.priced_jobs) {
        if (job.id == name) return &job;        
    }
    return nullptr;
}

u8 get_random_loc(Perception const& perc) {
    int count = 0;
    count += perc.charging_stations.size();
    count += perc.dump_locations.size();
    count += perc.shops.size();
    count += perc.storages.size();
    count += perc.workshops.size();

    int i = rand() % count;

    if (i < perc.charging_stations.size()) {
        return perc.charging_stations[i].name;
    }
    i -= perc.charging_stations.size();
    
    if (i < perc.dump_locations.size()) {
        return perc.dump_locations[i].name;
    }
    i -= perc.dump_locations.size();
    
    if (i < perc.shops.size()) {
        return perc.shops[i].name;
    }
    i -= perc.shops.size();
    
    if (i < perc.storages.size()) {
        return perc.storages[i].name;
    }
    i -= perc.storages.size();
    
    if (i < perc.workshops.size()) {
        return perc.workshops[i].name;
    }

    return 0;
}


Action const& random_agent (u8 id, Simulation const& sim, Perception const& perc) {
    static Buffer buffer;
    static u8 target[8];

    for (Charging_station const& i: perc.charging_stations) {
        if (i.name == perc.self.in_facility and perc.self.charge < sim.role.max_battery) {
            return buffer.emplace<Action_Charge>();
        }
    }
    
    if (perc.self.charge * sim.role.speed < 700) {
        int min_diff = 0x7fffffff;
        for (Charging_station const& i: perc.charging_stations) {
            auto const& p = perc.self.pos;
            auto const& q = i.pos;
            int diff = (p.lat-q.lat)*(p.lat-q.lat) + (p.lon-q.lon)*(p.lon-q.lon);
            diff = diff * 16 / (rand() % 16 + 8);
            if (diff < min_diff) {
                min_diff = diff;
                target[id] = i.name;
                return buffer.emplace<Action_Goto1>(0, target[id]);
            }
        }
    }
    
    if (perc.self.last_action_result == Action::SUCCESSFUL and perc.self.in_facility != target[id]) {
        return buffer.emplace<Action_Continue>();
    }

    target[id] = get_random_loc(perc);
    return buffer.emplace<Action_Goto1>(0, target[id]);
    

    /*
    Pos cur_goal;
    cur_goal.lat = rand() % 128 + 64;
    cur_goal.lon = rand() % 128 + 64;
    return buffer.emplace_back<Action_Goto2>(cur_goal);
    
    */

/*
    static u8 cur_job;
    static u8 cur_goal;
    static u8 cur_target;
    static u16 random = rand();

    buffer.reset();

    if (!cur_job and perc.priced_jobs.size()) {
        cur_job = perc.priced_jobs[random % perc.priced_jobs.size()].id;
    }
    if (cur_job) {
        Job_priced const* job = find_job(cur_job, perc);
        if (job) {
            int start = random % job->items.size();
            for (int ind = 0; ind < job->items.size(); ++ind) {
                int i = (ind + start) % job->items.size();
                Job_item item = job->items[i];
                if (item.delivered < item.amount) {
                    u8 source = find_item_source(item.item, sim, perc);
                    if (source) {
                        cur_target = source;
                        cur_goal = item.item;
                        jout << "We're going!\n";
                        return buffer.emplace_back<Action_Goto1>(source);
                    }
                }
            }
        }
    }

    buffer.reserve(cur_goal & cur_target);
    
    return buffer.emplace_back<Action_Skip>();
*/
}

} /* end of namespace jup */

