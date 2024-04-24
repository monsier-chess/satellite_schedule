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

#define MIN_SLOTS_ON_OBJECT 1

int days = 6;
int slots_per_day = 10;
int slots_window_per_day = 7;
int objects_type_a = 5;
int objects_type_b = 5;
int daily_views_of_object_type_a = 3;
int daily_views_of_object_type_b = 2;
int days_for_object_type_a = 1;
int days_for_object_type_b = 2;

int objects = objects_type_a + objects_type_b;
int rows = days * slots_per_day;
int cols = objects;

std::string filename = "data/tiny_limits.txt";

void fill_matrix_from_file(const std::string &filename, std::vector<bool> &matrix)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Can't open file" << std::endl;
        return;
    }
    std::string line;
    int row = 0;

    while (std::getline(file, line) && row < rows)
    {
        std::stringstream ss(line);
        std::string value;
        int col = 0;
        while (std::getline(ss, value, ',') && col < cols)
        {
            matrix[row * cols + col] = (std::stoi(value) == 1);
            col++;
        }
        row++;
    }
    file.close();
}

bool checkSolution(operations_research::sat::CpSolverResponse response,
                   operations_research::sat::BoolVar *schedule,
                   std::vector<bool> &matrix)
{
    // Can watch object if matrix[i][j] is 1
    bool cell;
    for (int i = 0; i < rows; i++)
    {
        for (int j = 0; j < cols; j++)
        {
            cell = SolutionBooleanValue(response, schedule[i * cols + j]);
            if (not matrix[i * cols + j] and cell)
            {
                std::cerr << "CANT WATCH THIS OBJECT IN THIS SLOTS" << std::endl;
                std::cerr << "slot: " << i << " object: " << j << std::endl;
                return false;
            }
        }
    }

    // Can't watch more than 1 object in slot
    std::vector<bool> slots(rows);
    bool slot_is_busy = false;
    for (int i = 0; i < rows; i++)
    {
        slot_is_busy = false;
        slots[i] = false;
        for (int j = 0; j < cols; j++)
        {
            if (not matrix[i * cols + j])
            {
                continue;
            }
            cell = SolutionBooleanValue(response, schedule[i * cols + j]);
            if (not cell)
            {
                continue;
            }
            if (slot_is_busy)
            {
                std::cerr << "SLOT IS BUSY" << std::endl;
                std::cerr << "slot: " << i << std::endl;
                return false;
            }
            slot_is_busy = true;
            slots[i] = true;
        }
    }

    int left, rigth;
    for (int day = 0; day < days; day++)
    {
        left = 0;
        rigth = 0;

        for (int i = slots_window_per_day + 1; i < slots_per_day; i++)
        {
            rigth += slots[day * slots_per_day + i];
        }

        for (int shift = 0; shift < slots_per_day - slots_window_per_day - 1; shift++)
        {
            left += slots[day * slots_per_day + shift];
            rigth -= slots[day * slots_per_day + shift + slots_window_per_day + 1];
            if ((left > 0) and (rigth > 0))
            {
                std::cerr << "WINDOW SIZE IS EXCEEDED" << std::endl;
                std::cerr << "day: " << day << std::endl;
                std::cerr << "shift: " << shift << std::endl;
                std::cerr << "left: " << left << std::endl;
                std::cerr << "rigth: " << rigth << std::endl;
                return false;
            }
        }
    }

    int daily_views_limit;
    int daily_views_of_object;
    int *days_on_objects = new int[objects];
    int days_on_object_limit;
    int global_slot;

    for (int object = 0; object < objects; object++)
    {
        days_on_objects[object] = 0;
    }

    for (int day = 0; day < days; day++)
    {
        for (int object = 0; object < objects; object++)
        {
            daily_views_of_object = 0;
            for (int slot = 0; slot < slots_per_day; slot++)
            {
                global_slot = day * slots_per_day + slot;
                cell = SolutionBooleanValue(response, schedule[global_slot * cols + object]);
                daily_views_of_object += cell;
            }
            if (daily_views_of_object == 0)
            {
                continue;
            }
            if (object < objects_type_a)
            {
                daily_views_limit = daily_views_of_object_type_a;
                days_on_object_limit = days_for_object_type_a;
            }
            else
            {
                daily_views_limit = daily_views_of_object_type_b;
                days_on_object_limit = days_for_object_type_b;
            }
            if (daily_views_of_object == daily_views_limit)
            {
                days_on_objects[object] += 1;
                if (days_on_objects[object] <= days_on_object_limit)
                {
                    continue;
                }
                std::cerr << "DAYS LIMIT IS EXCEEDED" << std::endl;
                std::cerr << "object: " << object << " have " << days_on_objects[object]
                          << " but limit is " << days_on_object_limit << std::endl;
            }
            std::cerr << "DAILY VIEWS INCORRECT COUNT" << std::endl;
            std::cerr << "object: " << object << " have " << daily_views_of_object
                      << " in day " << day << " but correct value is "
                      << daily_views_limit << std::endl;

        }
    }

    for (int object = 0; object < objects; object++)
    {
        if (object < objects_type_a)
        {
            days_on_object_limit = days_for_object_type_a;
        }
        else
        {
            days_on_object_limit = days_for_object_type_b;
        }
        if (days_on_objects[object] == days_on_object_limit)
        {
            continue;
        }
        std::cerr << "DAYS COUNT IS INCORRECT" << std::endl;
        std::cerr << "object: " << object << " have " << days_on_objects[object]
                  << " but needed " << days_on_object_limit << std::endl;

    }

    // 1 object must have at least MIN_SLOTS_ON_OBJECT slots

    int slots_on_obj;
    for (int j = 0; j < cols; j++)
    {
        slots_on_obj = 0;
        for (int i = 0; i < rows; i++)
        {
            cell = SolutionBooleanValue(response, schedule[i * cols + j]);
            slots_on_obj += cell;
            if (slots_on_obj >= MIN_SLOTS_ON_OBJECT)
            {
                break;
            }
        }
        if (slots_on_obj < MIN_SLOTS_ON_OBJECT)
        {
            std::cerr << "LESS THAN MINIMUM" << std::endl;
            std::cerr << "object: " << j << " have " << slots_on_obj << " but need at least " << MIN_SLOTS_ON_OBJECT << std::endl;
            return false;
        }
    }

    //
    return true;
}

namespace operations_research
{
    namespace sat
    {

        void make_schedule()
        {

            // Load data from file
            std::vector<bool> matrix(rows * cols);
            fill_matrix_from_file(filename, matrix);

            CpModelBuilder cp_model;

            BoolVar *schedule = new BoolVar[rows * cols];

            // Can watch object if matrix[i][j] is 1
            for (int i = 0; i < rows; i++)
            {
                for (int j = 0; j < cols; j++)
                {
                    if (matrix[i * cols + j])
                    {
                        schedule[i * cols + j] = cp_model.NewBoolVar();
                    }
                    else
                    {
                        schedule[i * cols + j] = cp_model.FalseVar();
                    }
                }
            }

            LinearExpr *slots = new LinearExpr[rows];

            LinearExpr only_one = LinearExpr(1);

            // Can't watch more than 1 object in slot
            for (int i = 0; i < rows; i++)
            {
                LinearExpr slots_object_count;
                for (int j = 0; j < cols; j++)
                {
                    if (not matrix[i * cols + j])
                    {
                        continue;
                    }
                    slots_object_count += schedule[i * cols + j];
                }
                slots[i] = slots_object_count;
                cp_model.AddLessOrEqual(slots_object_count, only_one);
            }

            // Can use slots in window size slots_window_per_day
            LinearExpr sum;
            for (int day = 0; day < days; day++)
            {
                for (int i = 0; i < slots_per_day - slots_window_per_day; i++)
                {
                    for (int j = i + slots_window_per_day; j < slots_per_day; j++)
                    {
                        sum = slots[day * slots_per_day + i] + slots[day * slots_per_day + j];
                        cp_model.AddLessOrEqual(sum, only_one);
                    }
                }
            }

            LinearExpr *views_of_object_in_day = new LinearExpr[days * objects];
            BoolVar *is_object_viewed_in_day = new BoolVar[days * objects];
            LinearExpr *all_object_views_days = new LinearExpr[objects];

            for (int i = 0; i < days; i++)
            {
                for (int j = 0; j < objects; j++)
                {
                    is_object_viewed_in_day[i * objects + j] = cp_model.NewBoolVar();
                }
            }

            for (int object = 0; object < objects; object++)
            {
                all_object_views_days[object] = LinearExpr();
            }

            // Control of objects daily views
            int daily_views;
            for (int day = 0; day < days; day++)
            {
                for (int object = 0; object < objects; object++)
                {
                    views_of_object_in_day[day * objects + object] = LinearExpr();
                    for (int slot = 0; slot < slots_per_day; slot++)
                    {
                        int global_slot = day * slots_per_day + slot;
                        if (not matrix[global_slot * objects + object])
                        {
                            continue;
                        }
                        views_of_object_in_day[day * objects + object] += schedule[global_slot * objects + object];
                    }

                    if (object < objects_type_a)
                    {
                        daily_views = daily_views_of_object_type_a;
                    }
                    else
                    {
                        daily_views = daily_views_of_object_type_b;
                    }

                    cp_model.AddEquality(
                        daily_views * is_object_viewed_in_day[day * objects + object],
                        views_of_object_in_day[day * objects + object]
                    );
                    all_object_views_days[object] += is_object_viewed_in_day[day * objects + object];
                }
            }

            // Objects days limit
            int days_for_object;
            LinearExpr days_for_obiect_type_a_lin_exp = LinearExpr(days_for_object_type_a);
            LinearExpr days_for_obiect_type_b_lin_exp = LinearExpr(days_for_object_type_b);
            LinearExpr days_for_obiect_lin_exp;

            for (int object = 0; object < objects; object++)
            {
                if (object < objects_type_a)
                {
                    days_for_obiect_lin_exp = days_for_obiect_type_a_lin_exp;
                }
                else
                {
                    days_for_obiect_lin_exp = days_for_obiect_type_b_lin_exp;
                }
                cp_model.AddEquality(
                    all_object_views_days[object],
                    days_for_obiect_lin_exp
                );
            }

            // Solving part
            const CpSolverResponse response = Solve(cp_model.Build());

            if (response.status() == CpSolverStatus::OPTIMAL ||
                response.status() == CpSolverStatus::FEASIBLE)
            {
                for (int i = 0; i < rows; i++)
                {
                    if (i % slots_per_day == 0)
                    {
                        std::cout << "Day " << i / slots_per_day << std::endl;
                    }
                    for (int j = 0; j < cols; j++)
                    {
                        std::cout << " #"[(int)SolutionBooleanValue(response, schedule[i * cols + j])] << " ";
                    }
                    std::cout << std::endl;
                }
                LOG(INFO) << checkSolution(response, schedule, matrix);
            }
            else
            {
                LOG(INFO) << "No solution found.";
            }

            // Statistics.
            LOG(INFO) << "Statistics";
            LOG(INFO) << CpSolverResponseStats(response);
        }

    } // namespace sat
} // namespace operations_research

int main()
{
    operations_research::sat::make_schedule();
    return EXIT_SUCCESS;
}
