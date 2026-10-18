#pragma once
#include <bit>
#include <cfenv>
#include <fstream>
#include <iomanip>
#if defined(_MSC_VER)
// Read-only diagnostic of the exact CRT dispatch flag seen in fma.obj.
// Never alter it or force an instruction-set path.
extern "C" int __fma3_is_available;
#endif
static void writeFmaResidualProof(const std::string& stem){
    std::ofstream output(stem+".csv");require(bool(output),"FMA evidence output");
    output<<std::setprecision(17)<<"a,b,product,fma_residual,pair_high,pair_low\n";
    auto emit=[&](double a,double b){const double product=a*b,residual=std::fma(a,b,-product);
        const auto pair=hem::bands::EqAccumulator::product(a,b);
        output<<a<<','<<b<<','<<product<<','<<residual<<','<<pair.high<<','<<pair.low<<'\n';};
    emit(1+std::ldexp(1.,-27),1-std::ldexp(1.,-27));
    emit(1+std::ldexp(1.,-52),1+std::ldexp(1.,-52));
    uint64_t seed=0x8319d923157fabc1ULL;
    auto random=[&]{seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
    auto number=[&](int minimum,int count){const auto bits=random();const uint64_t exponent=uint64_t(minimum+int(random()%uint64_t(count))+1023);
        return std::bit_cast<double>((bits&0x800fffffffffffffULL)|(exponent<<52));};
    // All products and residuals in this domain are normal doubles: no FTZ
    // exception can be misrepresented as a correctly-rounded residual.
    for(int i=0;i<20000;++i)emit(number(-400,801),number(-400,801));
    for(int i=0;i<10000;++i)emit(number(-20,27),number(-150,301));
    std::ifstream actual("fma-input-pairs.csv");std::string row;int actualPairs=0;
    while(std::getline(actual,row)){const auto comma=row.find(',');if(comma==std::string::npos)continue;
        emit(std::stod(row.substr(0,comma)),std::stod(row.substr(comma+1)));++actualPairs;}
    std::cout<<"FMA_RESIDUAL_VECTORS file="<<stem<<".csv random_and_edge=30002 actual_trace_pairs="<<actualPairs<<" rounding_mode="<<std::fegetround();
#if defined(_MSC_VER)
    std::cout<<" CRT___fma3_is_available="<<__fma3_is_available;
#endif
    std::cout<<"; Decimal validation is a separate required gate\n";
}
