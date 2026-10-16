// reduction
void parse(const uint8_t* data, int n, int32_t* pos, int32_t* pair, int* k) {
    uint32_t *Q,*E,*OP,*CL,*SEP; classify(data,n,Q,E,OP,CL,SEP);
    uint32_t *in_before=mask_alloc(n), *esc_before=mask_alloc(n);
    scan_compose(Q, E, n, in_before, esc_before);
    uint32_t *sf=mask_alloc(n);
    for (int i=0;i<n;i++)
        setbit(sf,i,(bit(OP,i)|bit(CL,i)|bit(SEP,i)) & ~bit(in_before,i));
    int kk = compact(data, sf, n, pos);

    int32_t *bp, *bc; int kb=0;
    gather_brackets(pos, kk, bp, bc, &kb);                       // [R6] 栈 -> 扫描+排序+配对
    int32_t *bal = prefix_sum_pm1(bc, kb);                       // [R6] 深度前缀和
    int32_t *key = malloc(kb*4);
    for (int j=0;j<kb;j++) key[j] = is_open(bc[j]) ? bal[j]-1 : bal[j];  // [R6]
    int32_t *ord = iota(kb);
    stable_sort_by_key(ord, kb, key);                            // [R6]
    for (int j=0;j<kk;j++) pair[j]=-1;
    pair_adjacent(bp, ord, kb, pair);                            // [R6]
    *k=kk;
}
