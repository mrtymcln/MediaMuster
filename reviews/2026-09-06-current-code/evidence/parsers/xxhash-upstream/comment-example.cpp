
   #include <string>
   #define XXH_STATIC_LINKING_ONLY // expose unstable API
   #include "xxhash.h"
   // Slow, seeds each time
   class HashSlow {
       XXH64_hash_t seed;
   public:
       HashSlow(XXH64_hash_t s) : seed{s} {}
       size_t operator()(const std::string& x) const {
           return size_t{XXH3_64bits_withSeed(x.c_str(), x.length(), seed)};
       }
   };
   // Fast, caches the seeded secret for future uses.
   class HashFast {
       unsigned char secret[XXH3_SECRET_DEFAULT_SIZE];
   public:
       HashFast(XXH64_hash_t s) {
           XXH3_generateSecret_fromSeed(secret, seed);
       }
       size_t operator()(const std::string& x) const {
           return size_t{
               XXH3_64bits_withSecret(x.c_str(), x.length(), secret, sizeof(secret))
           };
       }
   };
