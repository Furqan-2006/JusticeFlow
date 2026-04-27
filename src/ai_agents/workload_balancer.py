#!/usr/bin/env python3


import numpy as np
from scipy.optimize import linear_sum_assignment

def balance_workload():

    print("[AI: Workload] Initializing Workload Balancer (Hungarian Algorithm)...")
    
    # In the real system, you would query PostgreSQL for this data:
    # rows = Officers (e.g., Officer A, Officer B, Officer C)
    # cols = Open Cases (e.g., Case 1, Case 2, Case 3)
    
    officer_ids = [101, 102, 103]
    case_ids = [5001, 5002, 5003]
    
   
    
    print(f"[AI: Workload] Attempting to assign {len(case_ids)} cases to {len(officer_ids)} officers.")

    # The Cost Matrix. 
    # Value = (Distance to crime scene in km) + (Current active cases * 5 penalty points)
    # Lower is better.
    # 
    #          Case 1  Case 2  Case 3
    # Officer A [  15,     2,     8  ]
    # Officer B [   9,    12,     4  ]
    # Officer C [   5,     8,    14  ]
    
    cost_matrix = np.array([
        [15,  2,  8], 
        [ 9, 12,  4], 
        [ 5,  8, 14]
    ])

    # Execute the Hungarian Algorithm
    row_ind, col_ind = linear_sum_assignment(cost_matrix)
    
    
    
    
    
    
    
    

    # Process the optimal assignments
    total_system_cost = cost_matrix[row_ind, col_ind].sum()
    
 
    
    
    
    print("[AI: Workload] Optimal assignments found:")
    for i in range(len(row_ind)):
        officer = officer_ids[row_ind[i]]
        assigned_case = case_ids[col_ind[i]]
        cost = cost_matrix[row_ind[i], col_ind[i]]
        
        print(f" -> Assigned Case {assigned_case} to Officer {officer} (Cost: {cost})")
        
        # TODO: In Phase 5, we will write this assignment back to the Cases table in PostgreSQL





    print(f"[AI: Workload] Total system penalty score minimized to: {total_system_cost}")

if __name__ == "__main__":
    balance_workload()
