#pragma once

#include <cmath>
#include "cuckoo_base.h"

template<class T>
class CuckooTraits;

template<class K, class D, class HF = std::hash<K>,
         class Conf = Config<> >
class CuckooHomogeneous2L : public CuckooTraits<CuckooHomogeneous2L<K,D,HF,Conf> >::Base_t
{
private:
    using This_t         = CuckooHomogeneous2L<K,D,HF,Conf>;
    using Base_t         = typename CuckooTraits<This_t>::Base_t;
    friend Base_t;

public:
    static constexpr size_t bs = CuckooTraits<This_t>::bs;
    static constexpr size_t tl = CuckooTraits<This_t>::tl;
    static constexpr size_t nh = CuckooTraits<This_t>::nh;

private:
    using Bucket_t       = typename CuckooTraits<This_t>::Bucket_t;
    using Hasher_t       = typename CuckooTraits<This_t>::Hasher_t;
    using Hashed_t       = typename Hasher_t::Hashed_t;
    using Ext            = typename Hasher_t::Extractor_t;

public:
    using Key            = typename CuckooTraits<This_t>::Key;
    using Data           = typename CuckooTraits<This_t>::Data;

    CuckooHomogeneous2L(size_t cap = 0      , double size_constraint = 1.1,
                       size_t dis_steps = 0, size_t seed = 0)
        : Base_t(0, size_constraint, dis_steps, seed),
          ll_size(std::floor(double(cap) * size_constraint / double(tl*bs))),
          beta((size_constraint+1.)/2.), thresh(),
          factor(double(ll_size)/double(1ull << (32-ct_log(tl))))
    {
        for (size_t i = 0; i < tl; ++i)
        {
            ll_table[i] = std::make_unique<Bucket_t[]>(ll_size);
        }
        capacity    = tl * ll_size * bs;
        thresh      = double(capacity) / beta;

        //factor      = double(ll_size) / double(1ull << (32 - ct_log(tl)));
    }

    CuckooHomogeneous2L(const CuckooHomogeneous2L&) = delete;
    CuckooHomogeneous2L& operator=(const CuckooHomogeneous2L&) = delete;

    CuckooHomogeneous2L(CuckooHomogeneous2L&& rhs)
        : Base_t(std::move(rhs)), ll_size(rhs.ll_size), beta(rhs.beta),
          thresh(rhs.thresh), factor(rhs.factor)
    {
        for (size_t i = 0; i < tl; ++i)
        {
            ll_table[i] = std::move(rhs.ll_table[i]);
        }
    }

    CuckooHomogeneous2L& operator=(CuckooHomogeneous2L&& rhs)
    {
        Base_t::operator=(std::move(rhs));

        ll_size= rhs.ll_size;
        beta   = rhs.beta;
        thresh = rhs.thresh;
        factor = rhs.factor;

        for (size_t i = 0; i < tl; ++i)
        {
            std::swap(ll_table[i], rhs.ll_table[i]);
        }
        return *this;
    }

    ~CuckooHomogeneous2L()
    {
    }

    std::pair<size_t, Bucket_t*> getTable(size_t i)
    {
        return (i < tl) ? std::make_pair(ll_size, ll_table[i].get())
                        : std::make_pair(0,nullptr);
    }

    using Base_t::n;
    using Base_t::alpha;
    using Base_t::capacity;
    using Base_t::hasher;
    using Base_t::insert;

private:
    size_t                      ll_size;
    double                      beta;
    size_t                      thresh;
    double                      factor;

    std::unique_ptr<Bucket_t[]> ll_table[tl];
    std::vector<std::pair<Key, Data> > grow_buffer;

    inline void getBuckets(Hashed_t h, Bucket_t** mem) const
    {
        for (size_t i = 0; i < nh; ++i)
            mem[i] = getBucket(h,i);
    }

    inline Bucket_t* getBucket(Hashed_t h, size_t i) const
    {
        size_t tab = Ext::tab(h,i);
        return &(ll_table[tab][Ext::loc(h,i) * factor]);
    }

    inline void inc_n()
    {
        ++n;
        if (n > thresh) grow();
    }

    void grow()
    {
        size_t nll_size = std::floor(double(n)*alpha / double(tl*bs));
        nll_size = std::max(nll_size, ll_size+1);
        double nfactor  = double(nll_size)/double(1ull << (32-ct_log(tl)));

        for (size_t i = 0; i < tl; ++i)
        {
            auto ntable = std::make_unique<Bucket_t[]>(nll_size);
            migrate(i, ntable, nfactor);
            ll_table[i] = std::move(ntable);
        }

        ll_size  = nll_size;
        factor   = nfactor;
        capacity = ll_size*tl*bs;
        thresh   = double(n)*beta;
        if (grow_buffer.size()) finalize_grow();
    }

    inline void migrate(size_t ind, std::unique_ptr<Bucket_t[]>& target, double tfactor)
    {
        //auto& source = ll_table[ind];

        for(size_t i = 0; i < ll_size; ++i)
        {
            Bucket_t& bucket = ll_table[ind][i];

            for (size_t j = 0; j < bs; ++j)
            {
                auto element = bucket.elements[j];
                if (! element.first) break;
                auto hash = hasher(element.first);

                for (size_t ti = 0; ti < nh; ++ti)
                {
                    if ((ind == Ext::tab(hash,ti)) &&
                        (i   == size_t(Ext::loc(hash, ti)*factor)))
                    {
                        if (!target[Ext::loc(hash,ti) * tfactor].insert(element))
                            grow_buffer.push_back(element);
                    }
                }
            }
        }
    }

    inline void finalize_grow()
    {
        size_t temp = n;
        for (auto& e : grow_buffer)
        {
            insert(e);
        }
        n = temp;
        grow_buffer.clear();
    }
};

template<class K, class D, class HF,
         class Conf>
class CuckooTraits<CuckooHomogeneous2L<K,D,HF,Conf> >
{
public:
    using Specialized_t  = CuckooHomogeneous2L<K,D,HF,Conf>;
    using Base_t         = CuckooMultiBase<Specialized_t>;
    using Key            = K;
    using Data           = D;
    using Config_t       = Conf;

    static constexpr size_t tl = Config_t::tl;
    static constexpr size_t bs = Config_t::bs;
    static constexpr size_t nh = Config_t::nh;

    using Hasher_t       = Hasher<K, HF, ct_log(tl), nh, true, true>;
    using Bucket_t       = Bucket<K,D,bs>;
};
