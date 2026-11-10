#include"Bitcoin/Block.hpp"
#include"Bitcoin/TxId.hpp"
#include"Bitcoin/varint.hpp"
#include"Util/BacktraceException.hpp"
#include"Util/Str.hpp"
#include<algorithm>
#include<sstream>
#include<stdexcept>

namespace {

/* No block holds this many transactions; a count above it
 * is a parse gone wrong, not a block.  */
auto const max_txs = std::uint64_t(1000000);

/* The header Bitcoin began with.  */
auto const header_v1_size = std::size_t(80);
/* The BLAKE2b proof of work marks its redesigned header by
 * setting the top bit of the version word, and such a header
 * carries 84 further bytes (nonce2, nonce3, extranonce, time
 * offset, txcount, flags, XOR key and mask bits, height,
 * merge-mining hash).
 * We never hash the header, so we only need its width to
 * find where the transactions start.  */
auto const header_v2_extra = std::size_t(84);

}

namespace Bitcoin {

Block::Block(std::string const& s) : header(header_v1_size, 0) {
	auto buf = Util::Str::hexread(s);
	auto str = std::string(buf.begin(), buf.end());
	auto is = std::istringstream(std::move(str));
	is >> *this;
	if (!is.good())
		throw Util::BacktraceException<std::invalid_argument>("Bitcoin::Block: invalid hex string input.");
	if (is.get() != std::char_traits<char>::eof())
		throw Util::BacktraceException<std::invalid_argument>("Bitcoin::Block: input string too long.");
}

bool Block::contains(Bitcoin::TxId const& txid) const {
	return std::any_of( txs.begin(), txs.end()
			  , [&txid](Tx const& tx) {
		return tx.get_txid() == txid;
	});
}

Tx const* Block::spender( Bitcoin::TxId const& txid
			, std::uint32_t outnum
			) const {
	for (auto const& tx : txs)
		for (auto const& in : tx.inputs)
			if (in.prevTxid == txid && in.prevOut == outnum)
				return &tx;
	return nullptr;
}

}

std::ostream& operator<<(std::ostream& os, Bitcoin::Block const& v) {
	os.write((char const*) v.header.data(), v.header.size());
	auto len = std::uint64_t(v.txs.size());
	os << Bitcoin::varint(len);
	for (auto const& tx : v.txs)
		os << tx;
	return os;
}

std::istream& operator>>(std::istream& is, Bitcoin::Block& v) {
	v.header.resize(header_v1_size);
	is.read((char*) v.header.data(), header_v1_size);
	if (!is.good())
		return is;
	/* The version is little-endian, so its top bit is in
	 * the fourth byte.  */
	if (v.header[3] & 0x80) {
		v.header.resize(header_v1_size + header_v2_extra);
		is.read( (char*) v.header.data() + header_v1_size
		       , header_v2_extra
		       );
		if (!is.good())
			return is;
	}

	auto len = std::uint64_t();
	is >> Bitcoin::varint(len);
	if (len > max_txs) {
		is.setstate(std::ios_base::failbit);
		return is;
	}
	v.txs.resize(std::size_t(len));
	for (auto& tx : v.txs)
		is >> tx;

	return is;
}
