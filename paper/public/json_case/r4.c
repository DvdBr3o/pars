// scan
void parse(const uint8_t* data, int n, int32_t* pos, int32_t* pair, int* k) {
    uint32_t *Q,*E,*OP,*CL,*SEP; classify(data,n,Q,E,OP,CL,SEP);
    uint32_t *in_before=mask_alloc(n), *esc_before=mask_alloc(n);
    scan_compose(Q, E, n, in_before, esc_before);                 // [R4] 顺序循环 -> 并行扫描
    Stack st={0}; int m=0;
    for (int i=0;i<n;i++){
        if (bit(in_before,i)) continue;
        if      (bit(OP,i)) { int j=m; push(&st,j); pos[m]=i; pair[m]=-1; m++; }
        else if (bit(CL,i)) { int j=pop(&st); pos[m]=i; pair[m]=j; pair[j]=m; m++; }
        else if (bit(SEP,i)){ pos[m]=i; pair[m]=-1; m++; }
    }
    *k=m;
}
