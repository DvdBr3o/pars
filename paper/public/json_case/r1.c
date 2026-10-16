// classification
void parse(const uint8_t* data, int n, int32_t* pos, int32_t* pair, int* k) {
    uint32_t *Q,*E,*OP,*CL,*SEP; classify(data,n,Q,E,OP,CL,SEP);   // [R1] predicates -> masks
    bool in=false, esc=false; Stack st={0}; int m=0;
    for (int i=0;i<n;i++){
        bool isq=bit(Q,i), ise=bit(E,i);                          // [R1]
        bool masked = in;
        if (in){ if(esc) esc=false; else if(ise) esc=true; else if(isq) in=false; }  // [R1]
        else   { if(isq) in=true; }                                              // [R1]
        if (masked) continue;
        if      (bit(OP,i)) { int j=m; push(&st,j); pos[m]=i; pair[m]=-1; m++; }
        else if (bit(CL,i)) { int j=pop(&st); pos[m]=i; pair[m]=j; pair[j]=m; m++; }
        else if (bit(SEP,i)){ pos[m]=i; pair[m]=-1; m++; }
    }
    *k=m;
}
