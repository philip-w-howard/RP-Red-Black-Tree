BEGIN {test=RCU; nthreads=1; mode=1} 
/Test/ {test=$1 ; nthreads=$4 ; mode=$6 }
#/n_reads/ {print test, mode, nthreads, $2, $4, $6, $8, $10, $11, $12, $13, $14, $15, $16, $17} 
/n_reads/ {print mode "_" test, nthreads, $2, $8}
