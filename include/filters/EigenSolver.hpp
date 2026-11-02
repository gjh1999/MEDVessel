#pragma once
#include "core/Volume3D.hpp"
#include <algorithm>

namespace medvessel {
struct EigenResult3x3 {
    Point3D values{};
    std::array<Point3D,3> vectors{};
};
// Eigenvectors are paired with eigenvalues, sorted by increasing absolute value.
inline EigenResult3x3 eigenSymmetric3x3(std::array<double,9> matrix) {
    double magnitude=0;
    for(double value:matrix) {
        if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite matrix");
        magnitude=std::max(magnitude,std::abs(value));
    }
    for(int i=0;i<3;++i) for(int j=i+1;j<3;++j)
        if(std::abs(matrix[i*3+j]-matrix[j*3+i])>1e-12*magnitude)
            throw std::invalid_argument("Matrix must be symmetric");
    std::array<double,9> vectors{1,0,0,0,1,0,0,0,1};
    if(magnitude>0) for(auto& value:matrix) value/=magnitude;
    bool converged=false;
    for(int iteration=0;iteration<64;++iteration) {
        int p=0,q=1;
        for(int i=0;i<3;++i) for(int j=i+1;j<3;++j)
            if(std::abs(matrix[i*3+j])>std::abs(matrix[p*3+q])) {p=i;q=j;}
        double apq=matrix[p*3+q];
        if(std::abs(apq)<1e-15) {converged=true;break;}
        double angle=0.5*std::atan2(2*apq,matrix[q*3+q]-matrix[p*3+p]);
        double c=std::cos(angle),s=std::sin(angle);
        double app=matrix[p*3+p],aqq=matrix[q*3+q];
        for(int k=0;k<3;++k) if(k!=p && k!=q) {
            double akp=matrix[k*3+p],akq=matrix[k*3+q];
            matrix[k*3+p]=matrix[p*3+k]=c*akp-s*akq;
            matrix[k*3+q]=matrix[q*3+k]=s*akp+c*akq;
        }
        matrix[p*3+p]=c*c*app-2*s*c*apq+s*s*aqq;
        matrix[q*3+q]=s*s*app+2*s*c*apq+c*c*aqq;
        matrix[p*3+q]=matrix[q*3+p]=0;
        for(int k=0;k<3;++k) {
            double vp=vectors[k*3+p],vq=vectors[k*3+q];
            vectors[k*3+p]=c*vp-s*vq;
            vectors[k*3+q]=s*vp+c*vq;
        }
    }
    if(!converged) throw std::runtime_error("Jacobi iteration did not converge");
    std::array<int,3> order{0,1,2};
    std::stable_sort(order.begin(),order.end(),[&](int a,int b) {
        return std::abs(matrix[a*3+a])<std::abs(matrix[b*3+b]);
    });
    EigenResult3x3 result;
    for(int i=0;i<3;++i) {
        result.values[i]=matrix[order[i]*3+order[i]]*magnitude;
        for(int j=0;j<3;++j) result.vectors[i][j]=vectors[j*3+order[i]];
    }
    return result;
}
} // namespace medvessel
