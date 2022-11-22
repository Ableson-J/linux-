//#include "pch.h"
#include <iostream>
using namespace std;
class C {
public:
    C();
public:
    int i;
    int j;
    int k;
    double l;
};
C::C() {
    cout << "Create c obj" << endl;
    i = 3;
    j = 4;
    k = 5;
    l = 3.0;
}
int main()
{
    C *arc = new C[2];
    cout << arc  << endl;
    cout << arc + 1 << endl;
    cout << sizeof(*arc) << endl;
    cout << sizeof(*(arc + 1)) << endl;
    cout << arc->i << endl;
}
