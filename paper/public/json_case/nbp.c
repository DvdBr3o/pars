// nbp
void parse(const uint8_t* data, int n, int32_t* pos, int32_t* pair, int* k) {
    bool in=false, esc=false; Stack st={0}; int m=0;
    for (int i=0;i<n;i++){
        uint8_t c=data[i];
        bool masked = in;
        if (in){ if(esc) esc=false; else if(c=='\\') esc=true; else if(c=='"') in=false; }
        else   { if(c=='"') in=true; }
        if (masked) continue;
        if      (c=='{'||c=='[') { int j=m; push(&st,j); pos[m]=i; pair[m]=-1; m++; }
        else if (c=='}'||c==']') { int j=pop(&st); pos[m]=i; pair[m]=j; pair[j]=m; m++; }
        else if (c==','||c==':') { pos[m]=i; pair[m]=-1; m++; }
    }
    *k=m;
}
