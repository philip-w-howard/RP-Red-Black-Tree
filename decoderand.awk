BEGIN {test=RCU; nthreads=1; mode=1} 
/Test/ {test=$2 ; size=$3; readers=$5 ; writers=$7; mode=$9; updates=$12}
#/n_reads/ {print test, mode, nthreads, $2, $4, $6, $8, $10, $11, $12, $13, $14, $15, $16, $17} 
/n_reads/ {print test, mode, size, readers, writers, updates, $2+$3, $4+$5}
