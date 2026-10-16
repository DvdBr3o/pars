// compaction
void parse(const uint8_t* data, int n, int32_t* pos, int32_t* pair, int* k) {
    uint32_t *Q,*E,*OP,*CL,*SEP; classify(data,n,Q,E,OP,CL,SEP);
    uint32_t *in_before=mask_alloc(n), *esc_before=mask_alloc(n);
    scan_compose(Q, E, n, in_before, esc_before);
    uint32_t *sf = mask_alloc(n);
    for (int i=0;i<n;i++)
        setbit(sf, i, (bit(OP,i)|bit(CL,i)|bit(SEP,i)) & ~bit(in_before,i));  // [R5] select
    int kk = compact(data, sf, n, pos);                          // [R5] emit循环 -> compaction
    Stack st={0};
    for (int j=0;j<kk;j++){                                      // [R5] 在 pos 上做栈
        uint8_t c=data[pos[j]];
        if      (c=='{'||c=='[') { push(&st,j); pair[j]=-1; }
        else if (c=='}'||c==']') { int t=pop(&st); pair[j]=t; pair[t]=j; }
        else                     { pair[j]=-1; }
    }
    *k=kk;
}
