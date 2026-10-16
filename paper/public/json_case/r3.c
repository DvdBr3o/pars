// branch -> select
void parse(const uint8_t* data, int n, int32_t* pos, int32_t* pair, int* k) {
    uint32_t *Q,*E,*OP,*CL,*SEP; classify(data,n,Q,E,OP,CL,SEP);
    uint32_t *in_before=mask_alloc(n), *esc_before=mask_alloc(n);
    bool in=false, esc=false;
    for (int i=0;i<n;i++){
        in_before[i]=in; esc_before[i]=esc;
        bool isq=bit(Q,i), ise=bit(E,i);
        bool nin = (~in & isq) | (in & ~esc & ~isq) | (in & esc);   // [R3] if -> select
        bool nes = (in & ~esc & ise);                               // [R3]
        in=nin; esc=nes;                                            // [R3]
    }
    Stack st={0}; int m=0;
    for (int i=0;i<n;i++){
        if (bit(in_before,i)) continue;
        if      (bit(OP,i)) { int j=m; push(&st,j); pos[m]=i; pair[m]=-1; m++; }
        else if (bit(CL,i)) { int j=pop(&st); pos[m]=i; pair[m]=j; pair[j]=m; m++; }
        else if (bit(SEP,i)){ pos[m]=i; pair[m]=-1; m++; }
    }
    *k=m;
}
