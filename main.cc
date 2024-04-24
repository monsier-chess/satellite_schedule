#include <stdint.h>
#include <stdlib.h>
#include <vector>

#include <fstream>
#include <sstream>

#include <algorithm>

#include "ortools/base/logging.h"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/util/sorted_interval_list.h"

#include "absl/types/span.h"

int num_days = 6;
int num_daily_slots = 10;
int daily_slots_window_size = 7;
int num_objects_type_a = 5;
int num_objects_type_b = 5;
int daily_views_requirement_type_a = 3;
int daily_views_requirement_type_b = 2;
int days_requirement_type_a = 1;
int days_requirement_type_b = 2;

int num_total_objects = num_objects_type_a + num_objects_type_b;
int num_total_slots = num_days * num_daily_slots;

std::string availability_matrix_filename = "data/tiny_limits.txt";

void
load_matrix_from_file(const std::string& availability_matrix_filename,
                      std::vector<bool>& availability_matrix)
{
    std::ifstream file(availability_matrix_filename);
    if (!file.is_open()) {
        std::cerr << "Can't open file" << std::endl;
        return;
    }
    std::string line;
    int slot_index = 0;

    while (std::getline(file, line) && slot_index < num_total_slots) {
        std::stringstream ss(line);
        std::string value;
        int object_index = 0;
        while (std::getline(ss, value, ',') && object_index < num_total_objects) {
            availability_matrix[slot_index * num_total_objects + object_index] =
              (std::stoi(value) == 1);
            object_index++;
        }
        slot_index++;
    }
    file.close();
}

bool
validate_solution(operations_research::sat::CpSolverResponse response,
                  operations_research::sat::BoolVar* schedule,
                  std::vector<bool>& availability_matrix)
{
    // Can watch object if availability_matrix[slot_index][object_index] is true
    bool is_object_viewed;
    for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
        for (int object_index = 0; object_index < num_total_objects; object_index++) {
            is_object_viewed = SolutionBooleanValue(
              response, schedule[slot_index * num_total_objects + object_index]);
            if (not availability_matrix[slot_index * num_total_objects + object_index] and
                is_object_viewed) {
                std::cerr << "CANT WATCH THIS OBJECT IN THIS SLOTS" << std::endl;
                std::cerr << "slot: " << slot_index << " object: " << object_index << std::endl;
                return false;
            }
        }
    }

    // Can't watch more than 1 object_index in slot
    std::vector<bool> slots(num_total_slots);
    bool slot_is_busy = false;
    for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
        slot_is_busy = false;
        slots[slot_index] = false;
        for (int object_index = 0; object_index < num_total_objects; object_index++) {
            if (not availability_matrix[slot_index * num_total_objects + object_index]) {
                continue;
            }
            is_object_viewed = SolutionBooleanValue(
              response, schedule[slot_index * num_total_objects + object_index]);
            if (not is_object_viewed) {
                continue;
            }
            if (slot_is_busy) {
                std::cerr << "SLOT IS BUSY" << std::endl;
                std::cerr << "slot: " << slot_index << std::endl;
                return false;
            }
            slot_is_busy = true;
            slots[slot_index] = true;
        }
    }

    int left_slots_sum, rigth_slots_sum;
    for (int day_index = 0; day_index < num_days; day_index++) {
        left_slots_sum = 0;
        rigth_slots_sum = 0;

        for (int slot_index = daily_slots_window_size + 1; slot_index < num_daily_slots;
             slot_index++) {
            rigth_slots_sum += slots[day_index * num_daily_slots + slot_index];
        }

        for (int shift = 0; shift < num_daily_slots - daily_slots_window_size - 1; shift++) {
            left_slots_sum += slots[day_index * num_daily_slots + shift];
            rigth_slots_sum -=
              slots[day_index * num_daily_slots + shift + daily_slots_window_size + 1];
            if ((left_slots_sum > 0) and (rigth_slots_sum > 0)) {
                std::cerr << "WINDOW SIZE IS EXCEEDED" << std::endl;
                std::cerr << "day: " << day_index << std::endl;
                std::cerr << "shift: " << shift << std::endl;
                std::cerr << "left: " << left_slots_sum << std::endl;
                std::cerr << "rigth: " << rigth_slots_sum << std::endl;
                return false;
            }
        }
    }

    int daily_views_requirement;
    int days_requirement;
    int num_object_daily_views;
    int* objects_days_num = new int[num_total_objects];
    int global_slot_index;

    for (int object_index = 0; object_index < num_total_objects; object_index++) {
        objects_days_num[object_index] = 0;
    }

    for (int day_index = 0; day_index < num_days; day_index++) {
        for (int object_index = 0; object_index < num_total_objects; object_index++) {
            num_object_daily_views = 0;
            for (int day_slot_index = 0; day_slot_index < num_daily_slots; day_slot_index++) {
                global_slot_index = day_index * num_daily_slots + day_slot_index;
                is_object_viewed = SolutionBooleanValue(
                  response, schedule[global_slot_index * num_total_objects + object_index]);
                num_object_daily_views += is_object_viewed;
            }
            if (num_object_daily_views == 0) {
                continue;
            }
            if (object_index < num_objects_type_a) {
                daily_views_requirement = daily_views_requirement_type_a;
                days_requirement = days_requirement_type_a;
            } else {
                daily_views_requirement = daily_views_requirement_type_b;
                days_requirement = days_requirement_type_b;
            }
            if (num_object_daily_views == daily_views_requirement) {
                objects_days_num[object_index] += 1;
                if (objects_days_num[object_index] <= days_requirement) {
                    continue;
                }
                std::cerr << "DAYS LIMIT IS EXCEEDED" << std::endl;
                std::cerr << "object: " << object_index << " have "
                          << objects_days_num[object_index] << " but limit is " << days_requirement
                          << std::endl;
            }
            std::cerr << "DAILY VIEWS INCORRECT COUNT" << std::endl;
            std::cerr << "object: " << object_index << " have " << num_object_daily_views
                      << " in day " << day_index << " but correct value is "
                      << daily_views_requirement << std::endl;
        }
    }

    for (int object_index = 0; object_index < num_total_objects; object_index++) {
        if (object_index < num_objects_type_a) {
            days_requirement = days_requirement_type_a;
        } else {
            days_requirement = days_requirement_type_b;
        }
        if (objects_days_num[object_index] == days_requirement) {
            continue;
        }
        std::cerr << "DAYS COUNT IS INCORRECT" << std::endl;
        std::cerr << "object: " << object_index << " have " << objects_days_num[object_index]
                  << " but needed " << days_requirement << std::endl;
    }

    return true;
}

namespace operations_research {
namespace sat {

void
generate_schedule()
{

    // Load data from file
    std::vector<bool> availability_matrix(num_total_slots * num_total_objects);
    load_matrix_from_file(availability_matrix_filename, availability_matrix);

    CpModelBuilder cp_model;

    BoolVar* schedule = new BoolVar[num_total_slots * num_total_objects];

    // Can watch object if availability_matrix[slot_index][object_index] is 1
    for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
        for (int object_index = 0; object_index < num_total_objects; object_index++) {
            if (availability_matrix[slot_index * num_total_objects + object_index]) {
                schedule[slot_index * num_total_objects + object_index] = cp_model.NewBoolVar();
            } else {
                schedule[slot_index * num_total_objects + object_index] = cp_model.FalseVar();
            }
        }
    }

    LinearExpr* slots = new LinearExpr[num_total_slots];

    LinearExpr only_one = LinearExpr(1);

    // Can't watch more than 1 object in slotstd::cout << "#";
    for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
        LinearExpr slots_object_count;
        for (int object_index = 0; object_index < num_total_objects; object_index++) {
            if (not availability_matrix[slot_index * num_total_objects + object_index]) {
                continue;
            }
            slots_object_count += schedule[slot_index * num_total_objects + object_index];
        }
        slots[slot_index] = slots_object_count;
        cp_model.AddLessOrEqual(slots_object_count, only_one);
    }

    // Can use slots in window size daily_slots_window_size
    LinearExpr exterior_sum;
    int slot_global_index_1;
    int slot_global_index_2;
    for (int day_index = 0; day_index < num_days; day_index++) {
        for (int slot_index_1 = 0; slot_index_1 < num_daily_slots - daily_slots_window_size;
             slot_index_1++) {
            for (int slot_index_2 = slot_index_1 + daily_slots_window_size;
                 slot_index_2 < num_daily_slots;
                 slot_index_2++) {
                slot_global_index_1 = day_index * num_daily_slots + slot_index_1;
                slot_global_index_2 = day_index * num_daily_slots + slot_index_2;
                exterior_sum = slots[slot_global_index_1] + slots[slot_global_index_2];
                cp_model.AddLessOrEqual(exterior_sum, only_one);
            }
        }
    }

    LinearExpr* daily_object_views = new LinearExpr[num_days * num_total_objects];
    BoolVar* is_object_viewed_in_day = new BoolVar[num_days * num_total_objects];
    LinearExpr* total_object_days = new LinearExpr[num_total_objects];

    for (int day_index = 0; day_index < num_days; day_index++) {
        for (int slot_index = 0; slot_index < num_total_objects; slot_index++) {
            is_object_viewed_in_day[day_index * num_total_objects + slot_index] =
              cp_model.NewBoolVar();
        }
    }

    // Control of objects daily views
    int daily_views;
    for (int day_index = 0; day_index < num_days; day_index++) {
        for (int object_index = 0; object_index < num_total_objects; object_index++) {
            daily_object_views[day_index * num_total_objects + object_index] = LinearExpr();
            for (int slot_index = 0; slot_index < num_daily_slots; slot_index++) {
                int global_slot_index = day_index * num_daily_slots + slot_index;
                if (not availability_matrix[global_slot_index * num_total_objects + object_index]) {
                    continue;
                }
                daily_object_views[day_index * num_total_objects + object_index] +=
                  schedule[global_slot_index * num_total_objects + object_index];
            }

            if (object_index < num_objects_type_a) {
                daily_views = daily_views_requirement_type_a;
            } else {
                daily_views = daily_views_requirement_type_b;
            }

            cp_model.AddEquality(
              daily_views * is_object_viewed_in_day[day_index * num_total_objects + object_index],
              daily_object_views[day_index * num_total_objects + object_index]);
            total_object_days[object_index] +=
              is_object_viewed_in_day[day_index * num_total_objects + object_index];
        }
    }

    // Objects days limit
    int num_object_days;
    LinearExpr days_requirement_type_a_lin_exp = LinearExpr(days_requirement_type_a);
    LinearExpr days_requirement_type_b_lin_exp = LinearExpr(days_requirement_type_b);
    LinearExpr days_requirement_lin_exp;
    for (int object_index = 0; object_index < num_total_objects; object_index++) {
        if (object_index < num_objects_type_a) {
            days_requirement_lin_exp = days_requirement_type_a_lin_exp;
        } else {
            days_requirement_lin_exp = days_requirement_type_b_lin_exp;
        }
        cp_model.AddEquality(total_object_days[object_index], days_requirement_lin_exp);
    }

    // Solving part
    const CpSolverResponse response = Solve(cp_model.Build());

    if (response.status() == CpSolverStatus::OPTIMAL ||
        response.status() == CpSolverStatus::FEASIBLE) {
        for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
            if (slot_index % num_daily_slots == 0) {
                std::cout << "Day " << slot_index / num_daily_slots << std::endl;
            }
            for (int object_index = 0; object_index < num_total_objects; object_index++) {
                if (SolutionBooleanValue(response,
                                         schedule[slot_index * num_total_objects + object_index])) {
                    std::cout << "# ";
                } else {
                    std::cout << ". ";
                }
            }
            std::cout << std::endl;
        }
        LOG(INFO) << validate_solution(response, schedule, availability_matrix);
    } else {
        LOG(INFO) << "No solution found.";
    }

    // Statistics.
    LOG(INFO) << "Statistics";
    LOG(INFO) << CpSolverResponseStats(response);
}

} // namespace sat
} // namespace operations_research

int
main()
{
    operations_research::sat::generate_schedule();
    return EXIT_SUCCESS;
}
