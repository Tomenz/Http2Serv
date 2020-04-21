#ifndef SHA256_H
#define SHA256_H
#include <string>

class SHA256
{
    static const size_t SHA224_256_BLOCK_SIZE = (512 / 8);
public:
    void init();
    void update(const uint8_t *message, size_t len);
    void final(uint8_t *digest);

protected:
    void transform(const uint8_t *message, size_t block_nb);

public:
    static const uint32_t DIGEST_SIZE = (256 / 8);

protected:
    const static uint32_t sha256_k[];
    size_t      m_tot_len;
    size_t      m_len;
    uint8_t     m_block[2 * SHA224_256_BLOCK_SIZE];
    uint32_t    m_h[8];
};

std::string sha256(const std::string& input);
bool sha256(const std::string& input, char Buf[], size_t nBufLen);

#endif
