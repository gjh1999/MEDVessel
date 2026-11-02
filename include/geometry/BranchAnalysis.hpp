#pragma once
#include "core/Volume3D.hpp"
#include <algorithm>

namespace medvessel {
inline Point3D subtract(const Point3D& a,const Point3D& b) {
    return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};
}
inline double norm(const Point3D& p) {return std::hypot(p[0],std::hypot(p[1],p[2]));}
inline double polylineLengthMm(const std::vector<Point3D>& points) {
    double length=0;
    for(const auto& p:points) for(double v:p)
        if(!std::isfinite(v)) throw std::invalid_argument("Nonfinite centerline");
    for(std::size_t i=1;i<points.size();++i) length+=norm(subtract(points[i],points[i-1]));
    return length;
}
// Fit a local direction by least-squares regression on arc length, starting
// at the junction and pointing outward over a specified physical window.
inline Point3D branchDirection(const std::vector<Point3D>& points,double windowMm) {
    if(points.size()<2||!std::isfinite(windowMm)||windowMm<=0)
        throw std::invalid_argument("Invalid direction fitting window");
    polylineLengthMm(points);
    std::vector<Point3D> local{points.front()};std::vector<double> lengths{0};
    for(std::size_t i=1;i<points.size();++i) {
        double segment=norm(subtract(points[i],points[i-1]));
        if(segment==0) continue;
        double remaining=windowMm-lengths.back();
        if(segment>=remaining) {
            Point3D end{};
            for(int a=0;a<3;++a) end[a]=points[i-1][a]+remaining/segment*(points[i][a]-points[i-1][a]);
            local.push_back(end);lengths.push_back(windowMm);break;
        }
        local.push_back(points[i]);lengths.push_back(lengths.back()+segment);
    }
    if(local.size()<2) throw std::invalid_argument("Degenerate centerline");
    double mean=0;Point3D centre{};
    for(std::size_t i=0;i<local.size();++i) {
        mean+=lengths[i];for(int a=0;a<3;++a) centre[a]+=local[i][a];
    }
    mean/=local.size();for(auto& v:centre) v/=local.size();
    Point3D direction{};
    for(std::size_t i=0;i<local.size();++i) for(int a=0;a<3;++a)
        direction[a]+=(lengths[i]-mean)*(local[i][a]-centre[a]);
    double magnitude=norm(direction);
    if(magnitude==0) throw std::invalid_argument("Undefined branch direction");
    for(auto& v:direction) v/=magnitude;
    return direction;
}
inline double branchAngleDegrees(const std::vector<Point3D>& a,
    const std::vector<Point3D>& b,double windowMm) {
    auto u=branchDirection(a,windowMm),v=branchDirection(b,windowMm);
    double dot=0;for(int i=0;i<3;++i) dot+=u[i]*v[i];
    return std::acos(std::clamp(dot,-1.0,1.0))*180/std::acos(-1.0);
}
} // namespace medvessel
