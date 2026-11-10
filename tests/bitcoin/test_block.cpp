#undef NDEBUG
#include"Bitcoin/Block.hpp"
#include"Bitcoin/Tx.hpp"
#include"Bitcoin/TxId.hpp"
#include"Util/Str.hpp"
#include<assert.h>
#include<cstdint>
#include<sstream>
#include<string>

namespace {

/* Signet block 1, as lightningd's getrawblockbyheight returns
 * it: the header and one coinbase transaction.  */
char const block1_hex[] =
	"00000020f61eee3b63a380a477a063af32b2bbc97c9ff9f01f2c4225e9739881"
	"08000000f575c83235984e7dc4afc1f30944c170462e84437ab6f2d52e16878a"
	"79e4678bd1914d5fae77031eccf40700010100000000010100000000000000000"
	"00000000000000000000000000000000000000000000000ffffffff025151feff"
	"ffff0200f2052a010000001600149243f727dd5343293eb83174324019ec16c26"
	"30f0000000000000000776a24aa21a9ede2f61c3f71d1defd3fa999dfa3695375"
	"5c690689799962b48bebd836974e8cf94c4fecc7daa2490047304402205e423a8"
	"754336ca99dbe16509b877ef1bf98d008836c725005b3c787c41ebe4602204724"
	"6e4467ad7cc7f1ad98662afcaf14c115e0095a227c7b05c5182591c23e7e01000"
	"1200000000000000000000000000000000000000000000000000000000000000000"
	"00000000"
	;

/* Block 974606 of the BLAKE2b chain: a 164-byte header and
 * three transactions.  From the tests of
 * paulscode/lightning-fork-swap-backend.  */
char const block974606_hex[] =
	"000000a0cd02b5be5a62d5f2b7962470f85159aa4ba7cc69deb5c22600000000"
	"000000002d9961b20c046cee808c091a32cfa3128addb271588e3b8a21a73696"
	"2680c9668a1dbb6abaed00199eb5e6d34a3f00a88a1dbb6a00000000b18cf00d"
	"0d00000000000000000000000300000000000000000000000000000000000000"
	"0edf0e0000000000000000000000000000000000000000000000000000000000"
	"0000000003010000000001010000000000000000000000000000000000000000"
	"000000000000000000000000ffffffff22030edf0e0a536f6c6f410f56505300"
	"030e92100e84dd000000000000000000000000ffffffff023965a01200000000"
	"1976a9143d583eef453a9b4d827fce5e90535130273f60e988ac000000000000"
	"0000266a24aa21a9ed5a0fd40834760d4ef580fbc62ee9aa9eb3f095306420e3"
	"f737b519ffa85350f50120000000000000000000000000000000000000000000"
	"00000000000000000000000000000001000000000101addd0967b2b4d7e3f568"
	"4f419b3cbf148dc5076070d50bda4efe07dac8ac3dc90100000000fdffffff02"
	"4a4863000000000016001480355ca268f693f9dd36fa8688aa59a66cae62d4b2"
	"950a000000000016001479839d55852d1dce76cfc38aff329868b1d1c22f0247"
	"3044022075a0ab9e99de26fa7e89a62ce8ad0cb32be9f262a3895710f2de36fe"
	"c048381202201b3513e546dfb77c4baa2b8c9fad47248683af5cc7d0691ff023"
	"e541d9472cb901210203ca2ee45a82910c2138300f3c1674a6e15da77845a2ea"
	"784419f417ec1cb37c0000000002000000000101177711490655ddd50a609451"
	"34cf1ff3cc248f61dcec52a030e0c8de67b1a33c0200000000ffffffff017199"
	"0100000000001600140173415c405e20419b4bdaf0ebc975ca7841279f024830"
	"45022100a01e808dd55287fde73ab977a7f7bb5215b9f6463ef1c55e49b18998"
	"aa90cdf7022008edd1b6aa9493bd94808041c9f5a01928c5a3a36be9557f75a3"
	"422afdd8e11c01210312f32f6246ac3dd0f95ec4d30298f142a20a18bb517670"
	"9e21c0e174efde9adb00000000"
	;

}

int main() {
	auto block = Bitcoin::Block(std::string(block1_hex));
	assert(block.header.size() == 80);
	assert(block.txs.size() == 1);

	/* With one transaction, the merkle root in the header
	 * (bytes 36 to 67) is that transaction's id, in the same
	 * internal byte order the id serializes in.  */
	auto txid = block.txs[0].get_txid();
	auto os = std::ostringstream();
	os << txid;
	auto raw = os.str();
	assert(raw.size() == 32);
	for (auto i = std::size_t(0); i < 32; ++i)
		assert(std::uint8_t(raw[i]) == block.header[36 + i]);

	assert(block.contains(txid));
	assert(!block.contains(Bitcoin::TxId(std::string(
		"0000000000000000000000000000000000000000000000000000000000000001"
	))));

	/* The coinbase spends nothing; a block with a transaction
	 * spending an output names that transaction, for that
	 * output alone.  */
	assert(!block.spender(txid, 0));
	auto spend = Bitcoin::Tx();
	spend.inputs.resize(1);
	spend.inputs[0].prevTxid = txid;
	spend.inputs[0].prevOut = 1;
	spend.outputs.resize(1);
	auto spending_block = Bitcoin::Block();
	spending_block.txs.push_back(block.txs[0]);
	spending_block.txs.push_back(spend);
	assert(!spending_block.spender(txid, 0));
	auto found = spending_block.spender(txid, 1);
	assert(found);
	assert(found->get_txid() == spend.get_txid());

	/* Serializing gives the bytes back.  */
	auto bos = std::ostringstream();
	bos << block;
	auto bytes = bos.str();
	assert(Util::Str::hexdump(bytes.data(), bytes.size()) == block1_hex);

	/* A block of the BLAKE2b proof of work sets the top bit
	 * of the version and has 84 further header bytes; the
	 * transactions follow those.  */
	{
		auto hex = std::string(block1_hex);
		auto v2_hex = hex.substr(0, 6) + "a0"
			    + hex.substr(8, 160 - 8)
			    + std::string(84 * 2, '5')
			    + hex.substr(160)
			    ;
		auto v2 = Bitcoin::Block(v2_hex);
		assert(v2.header.size() == 164);
		assert(v2.header[3] == 0xa0);
		assert(v2.header[80] == 0x55 && v2.header[163] == 0x55);
		assert(v2.txs.size() == 1);
		assert(v2.contains(txid));
		auto v2os = std::ostringstream();
		v2os << v2;
		auto v2bytes = v2os.str();
		assert(Util::Str::hexdump(v2bytes.data(), v2bytes.size()) == v2_hex);

		/* Cut inside the further bytes, it does not parse.  */
		auto v2threw = false;
		try {
			auto bad = Bitcoin::Block(v2_hex.substr(0, 200));
			(void) bad;
		} catch (std::exception const&) {
			v2threw = true;
		}
		assert(v2threw);
	}

	/* A block from the BLAKE2b chain itself.  */
	{
		auto real = Bitcoin::Block(std::string(block974606_hex));
		assert(real.header.size() == 164);
		assert(real.txs.size() == 3);
		/* The header carries the height (0x0edf0e) before its
		 * last 32 bytes, and the coinbase carries it too.  */
		assert(real.header[128] == 0x0e);
		assert(real.header[129] == 0xdf);
		assert(real.header[130] == 0x0e);
		for (auto const& tx : real.txs)
			assert(real.contains(tx.get_txid()));
		auto prev = Bitcoin::TxId(std::string(
			"c93dacc8da07fe4eda0bd5706007c58d14bf3c9b414f68f5e3d7b4b26709ddad"
		));
		auto sp = real.spender(prev, 1);
		assert(sp);
		assert(sp->get_txid() == real.txs[1].get_txid());
		assert(!real.spender(prev, 0));
		auto ros = std::ostringstream();
		ros << real;
		auto rbytes = ros.str();
		assert(Util::Str::hexdump(rbytes.data(), rbytes.size()) == block974606_hex);
	}

	/* A truncated block does not parse.  */
	auto threw = false;
	try {
		auto bad = Bitcoin::Block(std::string(block1_hex, 200));
		(void) bad;
	} catch (std::exception const&) {
		threw = true;
	}
	assert(threw);

	return 0;
}
