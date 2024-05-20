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

const std::string DAYS_KEY = "--days";
const std::string SLOTS_KEY = "--slots";
const std::string OBJECTS_KEY = "--objects"; 
const std::string A_DAYS_KEY = "--a_days";
const std::string B_DAYS_KEY = "--b_days";
const std::string A_VIEWS_KEY = "--a_views";
const std::string B_VIEWS_KEY = "--b_views";
const std::string MATRIX_KEY = "--matrix";
const std::string TYPES_KEY = "--types";
const std::string RATIO_KEY = "--ratio";

const std::string DAYS_SHORT_KEY = "-d";
const std::string SLOTS_SHORT_KEY = "-s";
const std::string OBJECTS_SHORT_KEY = "-o";
const std::string MATRIX_SHORT_KEY = "-m";
const std::string TYPES_SHORT_KEY = "-t";
const std::string RATIO_SHORT_KEY = "-r";

int num_days = 6;
int num_daily_slots = 10;
int daily_slots_window_size = 7;
int days_requirement_type_a = 1;
int days_requirement_type_b = 2;
int daily_views_requirement_type_a = 3;
int daily_views_requirement_type_b = 2;
int num_objects = 10;
std::string availability_matrix_filename = "data/tiny_limits.txt";
std::string objects_type_filename = "data/tiny_objtype.txt";


int num_total_slots = num_days * num_daily_slots;
int progression_ratio = 2;

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
        while (std::getline(ss, value, ',') && object_index < num_objects) {
            availability_matrix[slot_index * num_objects + object_index] =
              (std::stoi(value) == 1);
            object_index++;
        }
        slot_index++;
    }
    file.close();
}

void
load_vector_from_file(const std::string& objects_type_filename, std::vector<bool>& objects_type) {
                      std::ifstream file(objects_type_filename);
    if (!file.is_open()) {
        std::cerr << "Can't open file" << std::endl;
        return;
    }
    std::string line;
    int index = 0;

    while (std::getline(file, line) && index < objects_type.size()) {
        objects_type[index] = (std::stoi(line) == 1);
        index++;
    }
    file.close();
}

bool
validate_solution(operations_research::sat::CpSolverResponse response,
                  operations_research::sat::BoolVar* schedule,
                  std::vector<bool>& availability_matrix,
                  std::vector<bool>& objects_type)
{
    // Can watch object if availability_matrix[slot_index][object_index] is true
    bool is_object_viewed;
    for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
        for (int object_index = 0; object_index < num_objects; object_index++) {
            is_object_viewed = SolutionBooleanValue(
              response, schedule[slot_index * num_objects + object_index]);
            if (not availability_matrix[slot_index * num_objects + object_index] and
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
        for (int object_index = 0; object_index < num_objects; object_index++) {
            if (not availability_matrix[slot_index * num_objects + object_index]) {
                continue;
            }
            is_object_viewed = SolutionBooleanValue(
              response, schedule[slot_index * num_objects + object_index]);
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
    int* objects_days_num = new int[num_objects];
    bool* is_object_viewed_in_day = new bool[num_days * num_objects];
    int global_slot_index;

    for (int object_index = 0; object_index < num_objects; object_index++) {
        objects_days_num[object_index] = 0;
    }

    for (int day_index = 0; day_index < num_days; day_index++) {
        for (int object_index = 0; object_index < num_objects; object_index++) {
            num_object_daily_views = 0;
            for (int day_slot_index = 0; day_slot_index < num_daily_slots; day_slot_index++) {
                global_slot_index = day_index * num_daily_slots + day_slot_index;
                is_object_viewed = SolutionBooleanValue(
                  response, schedule[global_slot_index * num_objects + object_index]);
                num_object_daily_views += is_object_viewed;
            }
            is_object_viewed_in_day[day_index * num_objects + object_index] = (num_object_daily_views > 0);
            if (num_object_daily_views == 0) {
                continue;
            }
            if (objects_type[object_index]) {
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
                std::cerr << "object " << object_index << " have "
                          << objects_days_num[object_index] << " but limit is " << days_requirement
                          << std::endl;
            }
            std::cerr << "DAILY VIEWS INCORRECT COUNT" << std::endl;
            std::cerr << "object " << object_index << " have " << num_object_daily_views
                      << " in day " << day_index << " but correct value is "
                      << daily_views_requirement << std::endl;
        }
    }

    for (int object_index = 0; object_index < num_objects; object_index++) {
        if (objects_type[object_index]) {
            days_requirement = days_requirement_type_a;
        } else {
            days_requirement = days_requirement_type_b;
        }
        if (objects_days_num[object_index] == days_requirement) {
            continue;
        }
        std::cerr << "DAYS COUNT IS INCORRECT" << std::endl;
        std::cerr << "object " << object_index << " have " << objects_days_num[object_index]
                  << " but needed " << days_requirement << std::endl;
    }

    // Geometric progression
    if ((days_requirement_type_b > 1) and (progression_ratio > 1)){
        int current_progression_element;
        int current_day_shift;    
        for (int object_index = 0; object_index < num_objects; object_index++) {
            if (objects_type[object_index]) {
                continue;
            }
            for (int day_index = 0; day_index < num_days; day_index++){
                if (not is_object_viewed_in_day[day_index * num_objects + object_index]){
                    continue;
                }
                current_progression_element = 1;
                current_day_shift = 0;
                for (int progression_index = 0; progression_index < days_requirement_type_b - 1; progression_index++){
                    current_day_shift += current_progression_element;
                    if ((day_index + current_day_shift) >= num_days) {
                        std::cerr << "GEOMETRIC PROGRESSION OUT OF RANGE" << std::endl;
                        std::cerr << "object " << object_index << " statred viewed in " << day_index
                                  << " and have to be viewed in " << day_index + current_day_shift
                                  << " but last day is " << num_days - 1 
                                  << std::endl;
                        return false;
                    }
                    if (not is_object_viewed_in_day[(day_index + current_day_shift) * num_objects + object_index]) {
                        std::cerr << "INCORRECT GEOMETRIC PROGRESSION" << std::endl;
                        std::cerr << "object " << object_index << " statred viewed in " << day_index
                                  << " and have to be viewed in " << day_index + current_day_shift
                                  << std::endl;
                        return false;
                    }
                    current_progression_element *= progression_ratio;
                }
                break;
            }
        }
    }
    return true;
}

namespace operations_research {
namespace sat {

void
generate_schedule()
{

    // Load data from files
    std::vector<bool> availability_matrix(num_total_slots * num_objects);
    load_matrix_from_file(availability_matrix_filename, availability_matrix);
    std::vector<bool> objects_type(num_objects);
    load_vector_from_file(objects_type_filename, objects_type);

    CpModelBuilder cp_model;

    BoolVar* schedule = new BoolVar[num_total_slots * num_objects];

    // Can watch object if availability_matrix[slot_index][object_index] is 1
    for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
        for (int object_index = 0; object_index < num_objects; object_index++) {
            if (availability_matrix[slot_index * num_objects + object_index]) {
                schedule[slot_index * num_objects + object_index] = cp_model.NewBoolVar();
            } else {
                schedule[slot_index * num_objects + object_index] = cp_model.FalseVar();
            }
        }
    }

    LinearExpr* slots = new LinearExpr[num_total_slots];

    LinearExpr only_one = LinearExpr(1);

    // Can't watch more than 1 object in slotstd::cout << "#";
    for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
        LinearExpr slots_object_count;
        for (int object_index = 0; object_index < num_objects; object_index++) {
            if (not availability_matrix[slot_index * num_objects + object_index]) {
                continue;
            }
            slots_object_count += schedule[slot_index * num_objects + object_index];
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

    LinearExpr* daily_object_views = new LinearExpr[num_days * num_objects];
    BoolVar* is_object_viewed_in_day = new BoolVar[num_days * num_objects];
    LinearExpr* total_object_days = new LinearExpr[num_objects];

    for (int day_index = 0; day_index < num_days; day_index++) {
        for (int object_index = 0; object_index < num_objects; object_index++) {
            is_object_viewed_in_day[day_index * num_objects + object_index] =
              cp_model.NewBoolVar();
        }
    }

    // Control of objects daily views
    int daily_views;
    for (int day_index = 0; day_index < num_days; day_index++) {
        for (int object_index = 0; object_index < num_objects; object_index++) {
            daily_object_views[day_index * num_objects + object_index] = LinearExpr();
            for (int slot_index = 0; slot_index < num_daily_slots; slot_index++) {
                int global_slot_index = day_index * num_daily_slots + slot_index;
                if (not availability_matrix[global_slot_index * num_objects + object_index]) {
                    continue;
                }
                daily_object_views[day_index * num_objects + object_index] +=
                  schedule[global_slot_index * num_objects + object_index];
            }

            if (objects_type[object_index]) {
                daily_views = daily_views_requirement_type_a;
            } else {
                daily_views = daily_views_requirement_type_b;
            }

            cp_model.AddEquality(
              daily_views * is_object_viewed_in_day[day_index * num_objects + object_index],
              daily_object_views[day_index * num_objects + object_index]);
            total_object_days[object_index] +=
              is_object_viewed_in_day[day_index * num_objects + object_index];
        }
    }

    // Objects days limit
    int num_object_days;
    LinearExpr days_requirement_type_a_lin_exp = LinearExpr(days_requirement_type_a);
    LinearExpr days_requirement_type_b_lin_exp = LinearExpr(days_requirement_type_b);
    LinearExpr days_requirement_lin_exp;
    for (int object_index = 0; object_index < num_objects; object_index++) {
        if (objects_type[object_index]) {
            days_requirement_lin_exp = days_requirement_type_a_lin_exp;
        } else {
            days_requirement_lin_exp = days_requirement_type_b_lin_exp;
        }
        cp_model.AddEquality(total_object_days[object_index], days_requirement_lin_exp);
    }

    // Geometry progression
    if ((days_requirement_type_b > 1) and (progression_ratio > 1)){
        int current_progression_element;
        int current_day_shift;
        int max_day_shift = pow(progression_ratio, days_requirement_type_b - 1) / (progression_ratio - 1);
        LinearExpr* object_days_prexif_sum = new LinearExpr[num_days * num_objects];

        // Prefix sum
        for (int day_index = 1; day_index < num_days - max_day_shift; day_index++){
            for (int object_index = 0; object_index < num_objects; object_index++) {
                if (objects_type[object_index]) {
                    continue;
                }
                object_days_prexif_sum[day_index * num_objects + object_index] = 
                    object_days_prexif_sum[(day_index - 1) * num_objects + object_index]
                    + is_object_viewed_in_day[(day_index - 1) * num_objects + object_index];
            }
        }

        // Checking for compliance with the geometric progression
        for (int day_index = 0; day_index < num_days - max_day_shift; day_index++){
            for (int object_index = 0; object_index < num_objects; object_index++) {
                if (objects_type[object_index]) {
                    continue;
                }
                current_progression_element = 1;
                current_day_shift = 0;
                for (int progression_index = 0; progression_index < days_requirement_type_b - 1; progression_index++){
                    current_day_shift += current_progression_element;
                    
                    // w_{i+shift} + prefix sum_{i} >= w_{i}
                    cp_model.AddGreaterOrEqual(
                        is_object_viewed_in_day[(day_index + current_day_shift) * num_objects + object_index]
                        + object_days_prexif_sum[day_index * num_objects + object_index],
                        is_object_viewed_in_day[day_index * num_objects + object_index]
                    );
                    
                    current_progression_element *= progression_ratio;
                }
            }
        }

        // Сan not use the last few days as the beginning of a sequence
        for (int day_index = num_days - max_day_shift; day_index < num_days; day_index++){
            for (int object_index = 0; object_index < num_objects; object_index++) {
                if (objects_type[object_index]) {
                    continue;
                }
                // Realy bad realization, but will work
                cp_model.AddEquality(is_object_viewed_in_day[day_index * num_objects + object_index], 0);
            }
        }
    }

    // Solving part
    const CpSolverResponse response = Solve(cp_model.Build());

    if (response.status() == CpSolverStatus::OPTIMAL or
        response.status() == CpSolverStatus::FEASIBLE) {
        for (int slot_index = 0; slot_index < num_total_slots; slot_index++) {
            if (slot_index % num_daily_slots == 0) {
                std::cout << "Day " << slot_index / num_daily_slots << std::endl;
            }
            for (int object_index = 0; object_index < num_objects; object_index++) {
                if (SolutionBooleanValue(response,
                                         schedule[slot_index * num_objects + object_index])) {
                    std::cout << "# ";
                } else {
                    std::cout << ". ";
                }
            }
            std::cout << std::endl;
        }
        LOG(INFO) << validate_solution(response, schedule, availability_matrix, objects_type);
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
main(int argc, char* argv[])
{
    int i = 1;
    while (i < argc){
        try {
            std::string cur_key(argv[i]);
            if ((cur_key == DAYS_KEY) or (cur_key == DAYS_SHORT_KEY)){
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                num_days = std::stoi(argv[i]);
            } else if ((cur_key == SLOTS_KEY) or (cur_key == SLOTS_SHORT_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                num_daily_slots = std::stoi(argv[i]);
            } else if ((cur_key == OBJECTS_KEY) or (cur_key == OBJECTS_SHORT_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                num_objects = std::stoi(argv[i]);
            } else if ((cur_key == A_VIEWS_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                daily_views_requirement_type_a = std::stoi(argv[i]);
            } else if ((cur_key == B_VIEWS_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                daily_views_requirement_type_b = std::stoi(argv[i]);
            } else if ((cur_key == A_DAYS_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                days_requirement_type_a = std::stoi(argv[i]);
            } else if ((cur_key == B_DAYS_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                days_requirement_type_b = std::stoi(argv[i]);  
            } else if ((cur_key == MATRIX_KEY) or (cur_key == MATRIX_SHORT_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                availability_matrix_filename = argv[i];
            } else if ((cur_key == TYPES_KEY) or (cur_key == TYPES_SHORT_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                objects_type_filename = argv[i];
            } else if ((cur_key == RATIO_KEY) or (cur_key == RATIO_SHORT_KEY)) {
                if (++i >= argc) {
                    std::cerr << "Flag " << argv[i-1] << " used without argument" << std::endl;
                    return 1;
                }
                progression_ratio = std::stoi(argv[i]);  
            } else {
                std::cerr << "Unknown option '" << argv[i] << "'" << std::endl;
                std::cerr << "Use " << DAYS_KEY << " (" << DAYS_SHORT_KEY << "), "
                                    << SLOTS_KEY << " (" << SLOTS_SHORT_KEY <<"), "
                                    << OBJECTS_KEY << " (" << OBJECTS_SHORT_KEY <<"), "
                                    << A_VIEWS_KEY << ", " << B_VIEWS_KEY << ", "
                                    << A_DAYS_KEY << ", " << B_DAYS_KEY << ", "
                                    << RATIO_KEY << " (" << RATIO_SHORT_KEY <<"), "
                                    << MATRIX_KEY << " (" << MATRIX_SHORT_KEY <<"), "
                                    << TYPES_KEY << " (" << TYPES_SHORT_KEY <<")"
                                    << std::endl;
                return 1;
            }
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid argument, after flag: " << argv[i-1] << " expected integer, but found: " << argv[i] << std::endl;
            return 1;
        } catch (const std::out_of_range& e) {
            std::cerr << "Argument out of range: " << argv[i] << std::endl;
            return 1;
        }
        i++;
    }
    num_total_slots = num_days * num_daily_slots;
    operations_research::sat::generate_schedule();
    return EXIT_SUCCESS;
}
