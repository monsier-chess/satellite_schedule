#include <stdint.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>

#include <algorithm>

#include "ortools/base/logging.h"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/util/sorted_interval_list.h"

#include "absl/types/span.h"

#define MIN_SLOTS_ON_OBJECT 2

#define TINY_VERSION

#ifdef TINY_VERSION
#define DAYS 5
#define SLOTS_PER_DAY 10
#define SLOTS_WINDOW_DER_DAY 4
#define OBJECTS 10
#define FILE_NAME "data/tiny_limits.txt"
#else
#define DAYS 180
#define SLOTS_PER_DAY 90
#define SLOTS_WINDOW_DER_DAY 20
#define OBJECTS 361
#define FILE_NAME "data/limits.txt"
#endif

#define ROWS (DAYS * SLOTS_PER_DAY)
#define COLS OBJECTS

void fill_matrix_from_file(const std::string &filename, std::bitset<ROWS * COLS> &matrix)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Can't open file" << std::endl;
        return;
    }
    std::string line;
    int row = 0;

    while (std::getline(file, line) && row < ROWS)
    {
        std::stringstream ss(line);
        std::string value;
        int col = 0;
        while (std::getline(ss, value, ',') && col < COLS)
        {
            if (std::stoi(value) == 1)
            {
                matrix.set(row * COLS + col);
            }
            else
            {
                matrix.reset(row * COLS + col);
            }
            col++;
        }
        row++;
    }
    file.close();
}

bool checkSolution(operations_research::sat::CpSolverResponse response,
                   operations_research::sat::BoolVar *schedule,
                   std::bitset<ROWS * COLS> matrix)
{
    // Can watch object if matrix[i][j] is 1
    bool cell;
    for (int i = 0; i < ROWS; i++)
    {
        for (int j = 0; j < COLS; j++)
        {
            cell = SolutionBooleanValue(response, schedule[i * COLS + j]);
            if (not matrix.test(i * COLS + j) and cell)
            {
                std::cerr << "CANT WATCH THIS OBJECT IN THIS SLOTS" << std::endl;
                std::cerr << "slot: " << i << " object: " << j << std::endl;
                return false;
            }
        }
    }

    // Can't watch more than 1 object in slot
    std::bitset<ROWS> slots;
    bool slot_is_busy = false;
    for (int i = 0; i < ROWS; i++)
    {
        slot_is_busy = false;
        slots.reset(i);
        for (int j = 0; j < COLS; j++)
        {
            cell = SolutionBooleanValue(response, schedule[i * COLS + j]);
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
            slots.set(i);
        }
    }

    int left, rigth;
    for (int day = 0; day < DAYS; day++)
    {
        left = 0;
        rigth = 0;

        for (int i = SLOTS_WINDOW_DER_DAY + 1; i < SLOTS_PER_DAY; i++)
        {
            rigth += slots.test(day * SLOTS_PER_DAY + i);
        }

        for (int shift = 0; shift < SLOTS_PER_DAY - SLOTS_WINDOW_DER_DAY - 1; shift++)
        {
            left += slots.test(day * SLOTS_PER_DAY + shift);
            //rigth -= slots.test(day * SLOTS_PER_DAY + shift); //
            rigth -= slots.test(day * SLOTS_PER_DAY + shift + SLOTS_WINDOW_DER_DAY + 1);
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

    // 1 object must have at least MIN_SLOTS_ON_OBJECT slots
    int slots_on_obj;
    for (int j = 0; j < COLS; j++)
    {
        slots_on_obj = 0;
        for (int i = 0; i < ROWS; i++)
        {
            cell = SolutionBooleanValue(response, schedule[i * COLS + j]);
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
            std::bitset<ROWS * COLS> matrix;
            fill_matrix_from_file(FILE_NAME, matrix);

            CpModelBuilder cp_model;

            BoolVar *schedule = new BoolVar[ROWS * COLS];

            // Can watch object if matrix[i][j] is 1
            for (int i = 0; i < ROWS; i++)
            {
                for (int j = 0; j < COLS; j++)
                {
                    if (matrix.test(i * COLS + j))
                    {
                        schedule[i * COLS + j] = cp_model.NewBoolVar();
                    }
                    else
                    {
                        schedule[i * COLS + j] = cp_model.FalseVar();
                    }
                }
            }

            LinearExpr *slots = new LinearExpr[ROWS];

            LinearExpr only_one = LinearExpr(1);

            // Can't watch more than 1 object in slot
            for (int i = 0; i < ROWS; i++)
            {
                LinearExpr slots_object_count(0);
                for (int j = 0; j < COLS; j++)
                {
                    slots_object_count += schedule[i * COLS + j];
                }
                slots[i] = slots_object_count;
                cp_model.AddLessOrEqual(slots_object_count, only_one);
            }

            // Can use slots in window size SLOTS_WINDOW_DER_DAY
            LinearExpr sum;
            for (int day = 0; day < DAYS; day++)
            {
                for (int i = 0; i < SLOTS_PER_DAY - SLOTS_WINDOW_DER_DAY; i++)
                {
                    for (int j = i + SLOTS_WINDOW_DER_DAY; j < SLOTS_PER_DAY; j++)
                    {
                        sum = slots[day * SLOTS_PER_DAY + i] + slots[day * SLOTS_PER_DAY + j];
                        cp_model.AddLessOrEqual(sum, 1);
                    }
                }
            }

            // 1 object must have at least MIN_SLOTS_ON_OBJECT slots
            for (int j = 0; j < COLS; j++)
            {
                LinearExpr object_slots_count(0);
                for (int i = 0; i < ROWS; i++)
                {
                    object_slots_count += schedule[i * COLS + j];
                }
                LinearExpr minimum = LinearExpr(MIN_SLOTS_ON_OBJECT);
                cp_model.AddGreaterOrEqual(object_slots_count, minimum);
            }

            // Solving part.
            const CpSolverResponse response = Solve(cp_model.Build());

            if (response.status() == CpSolverStatus::OPTIMAL ||
                response.status() == CpSolverStatus::FEASIBLE)
            {
                for (int i = 0; i < ROWS; i++)
                {
                    if (i % SLOTS_PER_DAY == 0)
                    {
                        std::cout << "Day " << i / SLOTS_PER_DAY << std::endl;
                    }
                    for (int j = 0; j < COLS; j++)
                    {
                        std::cout << (int)SolutionBooleanValue(response, schedule[i * COLS + j]) << " ";
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
