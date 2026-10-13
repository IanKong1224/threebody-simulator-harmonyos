#include "core/physics/InitialConditions.h"
#include <cmath>
#include <random>

namespace threebody {
namespace {

template <typename T>
ThreeBodyState<T> makeFigureEight() {
    ThreeBodyState<T> s{};
    s.body[0] = {{static_cast<T>(-0.97000436), static_cast<T>(0.24308753), 0},
                 {static_cast<T>(0.4662036850), static_cast<T>(0.4323657300), 0}, static_cast<T>(1)};
    s.body[1] = {{static_cast<T>(0.97000436), static_cast<T>(-0.24308753), 0},
                 {static_cast<T>(0.4662036850), static_cast<T>(0.4323657300), 0}, static_cast<T>(1)};
    s.body[2] = {{0, 0, 0},
                 {static_cast<T>(-0.93240737), static_cast<T>(-0.86473146), 0}, static_cast<T>(1)};
    return s;
}

} // namespace

ThreeBodyState<double> makeAsymmetricDemo() {
    ThreeBodyState<double> s{};
    s.body[0]={{-1.0,.20,.05},{.35,.45,.08},1.0};
    s.body[1]={{.95,-.24,-.08},{.52,.32,-.05},.85};
    s.body[2]={{.08,.08,.15},{-.65,-.62,.02},1.15};
    Vec3<double> center{},momentum{};double total=0;
    for(const auto& b:s.body){center+=b.position*b.mass;momentum+=b.velocity*b.mass;total+=b.mass;}
    for(auto& b:s.body){b.position-=center/total;b.velocity-=momentum/total;}
    return s;
}

// Screened non-hierarchical spatial initial state; see docs/PHYSICS.md.
ThreeBodyState<double> makeLongLivedTriple() {
    ThreeBodyState<double> s{};
    s.body[0]={{-0.9700332299302176,0.24309476493658094,0.00400309206643387},{0.46694397131126486,0.43019289811863415,0.03680121304874712},1.0058594083100505};
    s.body[1]={{0.9699754900697825,-0.24308029506341905,0.05130848968420293},{0.4588298638038786,0.4316148577981746,-0.013703009190557799},1.0059491604126436};
    s.body[2]={{-2.8869930217554556e-05,7.23493658096008e-06,-0.055430561774767434},{-0.9277294947892132,-0.8636286911067466,-0.023144749040243932},1.0037834392388674};
    return s;
}

// Hierarchical near-circular triple: 0.2 / 2 AU and 15 degree inclination.
ThreeBodyState<double> makeStableTriple() {
    ThreeBodyState<double> s{};
    const double m1=1.0, m2=.85, m3=1.15, inner=m1+m2, total=inner+m3;
    const double inclination=15.0*std::acos(-1.0)/180.0;
    const Vec3<double> r{.2,0,0}, v{0,std::sqrt(inner/.2),0};
    const Vec3<double> R{2.0,0,0};
    const double speed=std::sqrt(total/2.0);
    const Vec3<double> V{0,speed*std::cos(inclination),speed*std::sin(inclination)};
    const auto center=R*(-m3/total), velocity=V*(-m3/total);
    s.body[0]={center-r*(m2/inner),velocity-v*(m2/inner),m1};
    s.body[1]={center+r*(m1/inner),velocity+v*(m1/inner),m2};
    s.body[2]={R*(inner/total),V*(inner/total),m3};
    return s;
}

// Version 2: equal masses and fixed figure-eight velocities; only positions vary.
ThreeBodyState<double> makeRandomTriple(std::uint32_t seed) {
    std::mt19937 rng(seed);
    auto unit = [&]() { return static_cast<double>(rng()) / 4294967296.0; };
    auto vector = [&]() { return Vec3<double>{2*unit()-1,2*unit()-1,2*unit()-1}; };
    auto s=makeFigureEightDouble();
    // Finite rejection sampling; safe original positions serve as fallback.
    for(int attempt=0;attempt<256;++attempt){
        const auto p0=vector(),p1=vector(),p2=vector();
        if(norm(p0-p1)<.5||norm(p0-p2)<.5||norm(p1-p2)<.5)continue;
        s.body[0].position=p0;s.body[1].position=p1;s.body[2].position=p2;break;
    }
    Vec3<double> center{};
    for(const auto& b:s.body)center+=b.position;
    for(auto& b:s.body)b.position-=center/3.0;
    return s;
}

// Screened encounters and returns; finite validation, no prescribed trajectory.
ThreeBodyState<double> makeReturningTriple() {
    ThreeBodyState<double> s{};
    s.body[0]={{-0.21520279223720232,-0.4231910458765924,-0.40978452330455184},{0.18896100053152873,0.588177790647083,-0.8207425827717797},1.0};
    s.body[1]={{0.8839348823142549,-0.4037050185725093,0.3067716280929744},{-0.6613790974510905,-0.27514288199523157,0.6324670730664197},1.0};
    s.body[2]={{-0.6687320900770525,0.8268960644491017,0.10301289521157742},{0.4724180969195619,-0.31303490865185135,0.18827550970535994},1.0};
    return s;
}

ThreeBodyState<double> makeFigureEightDouble() { return makeFigureEight<double>(); }
ThreeBodyState<float> makeFigureEightFloat() { return makeFigureEight<float>(); }

} // namespace threebody
