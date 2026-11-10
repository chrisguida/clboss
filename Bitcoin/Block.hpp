#ifndef BITCOIN_BLOCK_HPP
#define BITCOIN_BLOCK_HPP
#include"Bitcoin/Tx.hpp"
#include<cstdint>
#include<iostream>
#include<string>
#include<vector>

namespace Bitcoin { class TxId; }

namespace Bitcoin {

/** class Bitcoin::Block
 *
 * @brief a block as the node serializes it: the
 * header and the transactions.
 * The header is 80 bytes, or 164 bytes for a block of the
 * BLAKE2b proof of work (top bit of the version set).
 */
class Block {
public:
	std::vector<std::uint8_t> header;
	std::vector<Tx> txs;

	Block() : header(80, 0) { }
	/* Parse the hex serialization of a block.  */
	explicit
	Block(std::string const&);

	/* Whether a transaction with the given id is in
	 * the block.  */
	bool contains(Bitcoin::TxId const&) const;

	/* The transaction in the block that spends the given
	 * output, or null when none does.  */
	Tx const* spender(Bitcoin::TxId const&, std::uint32_t outnum) const;
};

}

std::ostream& operator<<(std::ostream&, Bitcoin::Block const&);
std::istream& operator>>(std::istream&, Bitcoin::Block&);

#endif /* !defined(BITCOIN_BLOCK_HPP) */
