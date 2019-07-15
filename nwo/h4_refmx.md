# H4_REFMX : DP matrix for reference implementation

## Layout of each row dp[i]:


```
 dp[i]:   [ML MG IL IG DL DG] [ML MG IL IG DL DG] [ML MG IL IG DL DG]  ...  [ML MG IL IG DL DG]  [E  N  J  B  L  G  C JJ CC]
     k:   |------- 0 -------| |------- 1 -------| |------- 2 -------|  ...  |------- M -------|  
          |--------------------------------- (M+1)*h4R_NSCELLS -------------------------------|  |------ h4R_NXCELLS ------|

      The Validate() routine checks the following pattern: where * = -inf, . = calculated value, 0 = 0,
      and % = unused value that DP algorithm calculates, but is ignored and treated as -inf
 Forward:
     0:    *  *  *  *  *  *    *  *  *  *  *  *    *  *  *  *  *  *          *  *  *  *  *  *     *  0  *  .  .  .  *  *  *   
  1..L:    *  *  *  *  *  *    .  .  .  .  *  *    .  .  .  .  .  .          .  .  *  *  .  .     .  .  .  .  .  .  .  *  * 
 Backward:
      0:   *  *  *  *  *  *    *  *  *  *  *  *    *  *  *  *  *  *          *  *  *  *  *  *     %  .  %  .  .  .  %  *  *
 1..L-1:   *  *  *  *  *  *    .  .  .  .  .  .    .  .  .  .  .  .          .  .  *  *  .  .     .  .  .  .  .  .  .  *  *
      L:   *  *  *  *  *  *    .  .  .  .  .  .    .  .  .  .  .  .          .  .  *  *  .  .     .  *  *  *  *  *  .  *  *
 Decoding:
      0:   *  *  *  *  *  *    0  0  0  0  0  .    0  0  0  0  0  .          0  0  0  0  0  0     0  .  0  .  .  .  0  0  0 
      1:   *  *  *  *  *  *    .  .  .  .  0  .    .  .  .  .  .  .          .  .  0  0  .  .     .  .  .  .  .  .  .  0  0  
 2..L-1:   *  *  *  *  *  *    .  .  .  .  0  .    .  .  .  .  .  .          .  .  0  0  .  .     .  .  .  .  .  .  .  .  .
      L:   *  *  *  *  *  *    .  .  .  .  0  *    .  .  .  .  .  .          .  .  0  0  .  .     .  0  0  0  0  0  .  0  .
 Alignment:
      0:   *  *  *  *  *  *    *  *  *  *  *  *    *  *  *  *  *  *          *  *  *  *  *  *     *  .  *  .  .  .  *  *  *
 1..L-1:   *  *  *  *  *  *    .  .  .  .  *  *    .  .  .  .  .  .          .  .  *  *  .  .     .  .  .  .  .  .  .  *  *
      L:   *  *  *  *  *  *    .  .  .  .  *  *    .  .  .  .  .  .          .  .  *  *  .  .     .  *  *  *  *  *  .  *  *
```

## rationale:

   * k=0 column is only present for indexing k=1..M conveniently;
     all k=0 cells are $-\infty$.
   * i=0 row is Forward's initialization condition: only S $\rightarrow$ N $\rightarrow$ B $\rightarrow$ {LG} path prefix is
     possible, and S $\rightarrow$ N is 1.0 
   * i=0 row is Backward's termination condition: unneeded for
     posterior decoding; if we need Backwards score, we need N
     $\rightarrow$ B $\rightarrow$ {LG} $\rightarrow$ path
   * DL1 state removed by entry transition distributions (uniform entry)
   * DG1 state is also removed by G $\rightarrow$ Mk wing retracted
     entry in Fwd/Bck, but is valid in decoding because of G
     $\rightarrow$ DG1..DGk-1$\rightarrow$MGk wing unfolding
   * DL1 value is valid in Backward because it can be reached (via
     D$\rightarrow$E local exit) but isn't ever used; saves having to
     special case its nonexistence. 
   * DG1 value is valid in Backward because we intentionally leave
     D1$\rightarrow${DM} distribution in the `H4_PROFILE`, for use
     outside DP algorithms; 
     in h4_trace_Score() for example. Forward's initialization of DG1 to -inf is sufficient to make DG1 unused in Decoding.
   * ILm,IGm state never exists.
   * JJ,CC specials are only used in the Decoding matrix; they're decoded J$\rightarrow$J, C$\rightarrow$C transitions, for these states that emit on transition.
     N=NN for all i>=1, and NN=0 at i=0, so we don't need to store NN decoding.
 
## access:

| []()                            |   row ptr method               | macro method   |
|---------------------------------|--------------------------------|--------------------|
|  Row dp[i]:                     | `dpc = rx->dp[i]`              |                    |
|  Main state y at node k={0..M}: | `dpc[k*h4R_NSCELLS + y]`       | `H4R_MX(rx,i,k,y)` |
|  Special state y={ENJBLGC}:     | `dpc[(M+1)*h4R_NSCELLS + y]`   | `H4R_XMX(rx,i,y)`  |



If you need to treat all the floats in the matrix (or row)
identically, you can do loop over $L+1$ rows $i=0..L$, each of which
contains `(M+1)*h4R_NSCELLS + h4R_NXCELLS` contiguous floats that 
can be treated as an Easel float vector, e.g.:

```
   for (i = 0; i <= rx->L; i++) 
      esl_vec_FScale(rx->dp[i], (rx->M+1)*h4R_NSCELLS + h4R_NXCELLS, scale);
```

You can't use `esl_mat_*` matrix operations; because of how the DP
matrix is resized, DP rows are not necessarily contiguous in memory,
although values _within_ one row are.

