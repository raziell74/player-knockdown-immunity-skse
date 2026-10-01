#include "PCH.h"

#include "Hooks/KnockdownImmunity.h"
#include "Settings/Settings.h"

namespace
{
	constexpr float kKnockdownMagnitude = 1.0f;
	constexpr float kStaggerMagnitude = 0.99f;
	constexpr std::size_t kAbsoluteJumpSize = 14;

	struct Instruction
	{
		std::uint8_t length{ 0 };
		bool         ripRelative{ false };
		bool         relocatableDisp32{ false };
	};

	struct Prologue
	{
		struct Step
		{
			std::uint8_t offset{ 0 };
			Instruction  instruction{};
		};

		std::uint8_t length{ 0 };
		std::uint8_t count{ 0 };
		Step         steps[8]{};
	};

	constexpr bool HasModRM(std::uint8_t a_opcode)
	{
		switch (a_opcode) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x20:
		case 0x21:
		case 0x22:
		case 0x23:
		case 0x28:
		case 0x29:
		case 0x2A:
		case 0x2B:
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x38:
		case 0x39:
		case 0x3A:
		case 0x3B:
		case 0x63:
		case 0x69:
		case 0x6B:
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8C:
		case 0x8D:
		case 0x8E:
		case 0x8F:
		case 0xC0:
		case 0xC1:
		case 0xC6:
		case 0xC7:
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
		case 0xD8:
		case 0xD9:
		case 0xDA:
		case 0xDB:
		case 0xDC:
		case 0xDD:
		case 0xDE:
		case 0xDF:
		case 0xF6:
		case 0xF7:
		case 0xFE:
		case 0xFF:
			return true;
		default:
			return false;
		}
	}

	constexpr bool IsLegacyPrefix(std::uint8_t a_byte)
	{
		switch (a_byte) {
		case 0xF0:
		case 0xF2:
		case 0xF3:
		case 0x2E:
		case 0x36:
		case 0x3E:
		case 0x26:
		case 0x64:
		case 0x65:
		case 0x66:
		case 0x67:
			return true;
		default:
			return false;
		}
	}

	struct ModRM
	{
		std::uint8_t length{ 0 };
		bool         ripRelative{ false };
	};

	constexpr ModRM ReadModRM(const std::uint8_t* a_modrm, bool a_address32)
	{
		const auto modrm = *a_modrm;
		const auto mod = static_cast<std::uint8_t>(modrm >> 6);
		const auto rm = static_cast<std::uint8_t>(modrm & 7);
		ModRM      result{ 1, false };
		if (mod == 3) {
			return result;
		}

		const bool sib = rm == 4;
		if (sib) {
			result.length = static_cast<std::uint8_t>(result.length + 1);
			const auto base = static_cast<std::uint8_t>(a_modrm[1] & 7);
			if (mod == 0 && base == 5) {
				result.length = static_cast<std::uint8_t>(result.length + 4);
			}
		}

		if (mod == 1) {
			result.length = static_cast<std::uint8_t>(result.length + 1);
		} else if (mod == 2) {
			result.length = static_cast<std::uint8_t>(result.length + 4);
		} else if (!sib && rm == 5) {
			result.length = static_cast<std::uint8_t>(result.length + 4);
			result.ripRelative = !a_address32;
		}

		return result;
	}

	constexpr int ImmediateSize(std::uint8_t a_opcode, std::uint8_t a_modrm, bool a_operand16)
	{
		switch (a_opcode) {
		case 0x6B:
		case 0x80:
		case 0x82:
		case 0x83:
		case 0xC0:
		case 0xC1:
		case 0xC6:
			return 1;
		case 0x69:
		case 0x81:
		case 0xC7:
			return a_operand16 ? 2 : 4;
		case 0xF6:
			return ((a_modrm >> 3) & 7) <= 1 ? 1 : 0;
		case 0xF7:
			return ((a_modrm >> 3) & 7) <= 1 ? (a_operand16 ? 2 : 4) : 0;
		default:
			return 0;
		}
	}

	constexpr Instruction Fail()
	{
		return {};
	}

	constexpr Instruction Make(std::size_t a_length, bool a_ripRelative, bool a_relocatableDisp32)
	{
		if (a_length == 0 || a_length > 15) {
			return Fail();
		}

		return {
			static_cast<std::uint8_t>(a_length),
			a_ripRelative,
			a_relocatableDisp32
		};
	}

	constexpr Instruction DecodeOne(const std::uint8_t* a_code)
	{
		const auto* cursor = a_code;
		bool        operand16 = false;
		bool        address32 = false;
		bool        rexW = false;
		bool        sawRex = false;

		for (;;) {
			const auto byte = *cursor;
			const bool rex = byte >= 0x40 && byte <= 0x4F;
			if (!rex && !IsLegacyPrefix(byte)) {
				break;
			}
			if (sawRex || static_cast<std::size_t>(cursor - a_code) >= 14) {
				return Fail();
			}
			if (byte == 0x66) {
				operand16 = true;
			} else if (byte == 0x67) {
				address32 = true;
			}
			if (rex) {
				sawRex = true;
				rexW = (byte & 0x08) != 0;
			}
			++cursor;
		}

		const auto opcode = *cursor++;
		if (opcode == 0x0F) {
			const auto second = *cursor++;
			if (second >= 0x80 && second <= 0x8F) {
				cursor += 4;
				return Make(static_cast<std::size_t>(cursor - a_code), true, true);
			}
			if (second != 0x1E && second != 0x1F) {
				return Fail();
			}

			const auto modrm = ReadModRM(cursor, address32);
			cursor += modrm.length;
			return Make(static_cast<std::size_t>(cursor - a_code), modrm.ripRelative, modrm.ripRelative);
		}

		if (opcode == 0xE8 || opcode == 0xE9) {
			cursor += 4;
			return Make(static_cast<std::size_t>(cursor - a_code), true, true);
		}

		if (opcode == 0xEB || (opcode >= 0x70 && opcode <= 0x7F) || (opcode >= 0xE0 && opcode <= 0xE3)) {
			cursor += 1;
			return Make(static_cast<std::size_t>(cursor - a_code), true, false);
		}

		if (HasModRM(opcode)) {
			const auto modrmByte = *cursor;
			const auto modrm = ReadModRM(cursor, address32);
			cursor += modrm.length;
			const auto immediate = ImmediateSize(opcode, modrmByte, operand16);
			cursor += immediate;
			const bool relocatable = modrm.ripRelative && immediate == 0;
			return Make(static_cast<std::size_t>(cursor - a_code), modrm.ripRelative, relocatable);
		}

		switch (opcode) {
		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x56:
		case 0x57:
		case 0x58:
		case 0x59:
		case 0x5A:
		case 0x5B:
		case 0x5C:
		case 0x5D:
		case 0x5E:
		case 0x5F:
		case 0x90:
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
		case 0x98:
		case 0x99:
		case 0x9C:
		case 0x9D:
		case 0x9E:
		case 0x9F:
		case 0xC3:
		case 0xC9:
		case 0xCB:
		case 0xCC:
		case 0xF5:
		case 0xF8:
		case 0xF9:
		case 0xFA:
		case 0xFB:
		case 0xFC:
		case 0xFD:
			break;
		case 0x04:
		case 0x0C:
		case 0x14:
		case 0x1C:
		case 0x24:
		case 0x2C:
		case 0x34:
		case 0x3C:
		case 0x6A:
		case 0xA8:
		case 0xB0:
		case 0xB1:
		case 0xB2:
		case 0xB3:
		case 0xB4:
		case 0xB5:
		case 0xB6:
		case 0xB7:
		case 0xCD:
			cursor += 1;
			break;
		case 0x05:
		case 0x0D:
		case 0x15:
		case 0x1D:
		case 0x25:
		case 0x2D:
		case 0x35:
		case 0x3D:
		case 0x68:
		case 0xA9:
			cursor += operand16 ? 2 : 4;
			break;
		case 0xB8:
		case 0xB9:
		case 0xBA:
		case 0xBB:
		case 0xBC:
		case 0xBD:
		case 0xBE:
		case 0xBF:
			if (rexW) {
				cursor += 8;
			} else {
				cursor += operand16 ? 2 : 4;
			}
			break;
		case 0xA0:
		case 0xA1:
		case 0xA2:
		case 0xA3:
			cursor += address32 ? 4 : 8;
			break;
		case 0xC2:
		case 0xCA:
			cursor += 2;
			break;
		default:
			return Fail();
		}

		return Make(static_cast<std::size_t>(cursor - a_code), false, false);
	}

	constexpr Prologue ReadPrologue(const std::uint8_t* a_code)
	{
		Prologue prologue;
		while (prologue.length < 5 && prologue.count < static_cast<std::uint8_t>(std::size(prologue.steps))) {
			const auto instruction = DecodeOne(a_code + prologue.length);
			if (instruction.length == 0 || (instruction.ripRelative && !instruction.relocatableDisp32)) {
				return {};
			}

			const auto next = static_cast<std::size_t>(prologue.length) + instruction.length;
			if (next > 15) {
				return {};
			}

			auto& step = prologue.steps[prologue.count++];
			step.offset = prologue.length;
			step.instruction = instruction;
			prologue.length = static_cast<std::uint8_t>(next);
		}

		if (prologue.length < 5) {
			return {};
		}

		return prologue;
	}

	constexpr std::uint8_t kMovRsp[] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
	constexpr std::uint8_t kPushSub[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20 };
	constexpr std::uint8_t kPushThenSub[] = { 0x53, 0x48, 0x83, 0xEC, 0x20 };
	constexpr std::uint8_t kSubRsp32[] = { 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 };
	constexpr std::uint8_t kRipMov[] = { 0x48, 0x8B, 0x05, 0x78, 0x56, 0x34, 0x12 };
	constexpr std::uint8_t kShortJump[] = { 0xEB, 0x00, 0x90, 0x90, 0x90 };

	static_assert(DecodeOne(kMovRsp).length == 5);
	static_assert(!DecodeOne(kMovRsp).ripRelative);
	static_assert(ReadPrologue(kMovRsp).length == 5);
	static_assert(DecodeOne(kPushSub).length == 2);
	static_assert(DecodeOne(kPushSub + 2).length == 4);
	static_assert(ReadPrologue(kPushSub).length == 6);
	static_assert(ReadPrologue(kPushSub).count == 2);
	static_assert(ReadPrologue(kPushThenSub).length == 5);
	static_assert(DecodeOne(kSubRsp32).length == 7);
	static_assert(ReadPrologue(kSubRsp32).length == 7);
	static_assert(DecodeOne(kRipMov).length == 7);
	static_assert(DecodeOne(kRipMov).ripRelative);
	static_assert(DecodeOne(kRipMov).relocatableDisp32);
	static_assert(ReadPrologue(kRipMov).length == 7);
	static_assert(ReadPrologue(kShortJump).length == 0);

	void WriteAbsoluteJump(std::uint8_t* a_dst, std::uintptr_t a_target)
	{
		a_dst[0] = 0xFF;
		a_dst[1] = 0x25;
		a_dst[2] = 0x00;
		a_dst[3] = 0x00;
		a_dst[4] = 0x00;
		a_dst[5] = 0x00;
		std::memcpy(a_dst + 6, &a_target, sizeof(a_target));
	}

	bool RelocateDisp32(std::uint8_t* a_cave, std::uintptr_t a_target, const Prologue::Step& a_step)
	{
		const auto length = a_step.instruction.length;
		auto*      disp = reinterpret_cast<std::int32_t*>(a_cave + a_step.offset + length - 4);
		const auto nextOriginal = a_target + a_step.offset + length;
		const auto nextCave = reinterpret_cast<std::uintptr_t>(a_cave) + a_step.offset + length;
		const auto absolute = static_cast<std::int64_t>(nextOriginal) + *disp;
		const auto relocated = absolute - static_cast<std::int64_t>(nextCave);
		constexpr auto kMin = static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)());
		constexpr auto kMax = static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)());
		if (relocated < kMin || relocated > kMax) {
			return false;
		}

		*disp = static_cast<std::int32_t>(relocated);
		return true;
	}

	void LogPrologue(std::string_view a_name, std::uintptr_t a_target)
	{
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_target);
		SKSE::log::error(
			"Failed to hook {} at {:X}; prologue {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
			a_name,
			a_target,
			bytes[0],
			bytes[1],
			bytes[2],
			bytes[3],
			bytes[4],
			bytes[5],
			bytes[6],
			bytes[7]);
	}

	template <class Fn>
	bool Detour(std::uintptr_t a_target, Fn* a_hook, Fn*& a_original, std::string_view a_name)
	{
		if (!a_target) {
			SKSE::log::error("Failed to hook {}: address was 0", a_name);
			return false;
		}

		const auto prologue = ReadPrologue(reinterpret_cast<const std::uint8_t*>(a_target));
		if (prologue.length < 5) {
			LogPrologue(a_name, a_target);
			return false;
		}

		const std::size_t branchSize = prologue.length == 6 ? 6 : 5;
		const auto        caveSize = static_cast<std::size_t>(prologue.length) + kAbsoluteJumpSize;
		auto&             trampoline = SKSE::GetTrampoline();
		if (trampoline.free_size() < caveSize + kAbsoluteJumpSize) {
			SKSE::log::error("Trampoline is too small to hook {}", a_name);
			return false;
		}

		auto* cave = static_cast<std::uint8_t*>(trampoline.allocate(caveSize));
		std::memcpy(cave, reinterpret_cast<const void*>(a_target), prologue.length);
		for (std::uint8_t i = 0; i < prologue.count; ++i) {
			const auto& step = prologue.steps[i];
			if (!step.instruction.relocatableDisp32) {
				continue;
			}
			if (!RelocateDisp32(cave, a_target, step)) {
				SKSE::log::error("Failed to hook {}: RIP-relative displacement is out of range", a_name);
				return false;
			}
		}

		WriteAbsoluteJump(cave + prologue.length, a_target + prologue.length);
		REX::W32::FlushInstructionCache(REX::W32::GetCurrentProcess(), cave, caveSize);

		std::uint8_t original[15]{};
		std::memcpy(original, reinterpret_cast<const void*>(a_target), prologue.length);
		if (prologue.length > branchSize) {
			const auto tail = prologue.length - branchSize;
			if (!REL::safe_fill(a_target + branchSize, 0x90, tail, original + branchSize, tail)) {
				LogPrologue(a_name, a_target);
				return false;
			}
		}

		a_original = reinterpret_cast<Fn*>(cave);
		const auto hook = SKSE::stl::unrestricted_cast<std::uintptr_t>(a_hook);
		if (branchSize == 6) {
			trampoline.write_branch<6>(a_target, hook);
		} else {
			trampoline.write_branch<5>(a_target, hook);
		}

		SKSE::log::info(
			"Hooked {} at {:X} ({} byte prologue, {} byte branch)",
			a_name,
			a_target,
			prologue.length,
			branchSize);
		return true;
	}

	bool ShouldConvert(RE::Actor* a_actor)
	{
		const auto& config = Settings::Get();
		if (!config.enabled) {
			return false;
		}
		if (!a_actor || !a_actor->IsPlayerRef()) {
			return false;
		}
		if (a_actor->GetLifeState() != RE::ACTOR_LIFE_STATE::kAlive) {
			return false;
		}
		if (a_actor->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
			return false;
		}
		if (a_actor->IsInRagdollState()) {
			return false;
		}
		return !config.combatOnly || a_actor->IsInCombat();
	}

	const RE::BSFixedString* StaggerMagnitudeName()
	{
		static const RE::BSFixedString* name = nullptr;
		if (name && !name->empty()) {
			return name;
		}

		const auto* strings = RE::FixedStrings::GetSingleton();
		if (!strings || strings->staggerMagnitude.empty()) {
			return nullptr;
		}

		name = &strings->staggerMagnitude;
		return name;
	}

	float StaggerDirection(const RE::Actor& a_actor, const RE::NiPoint3& a_source)
	{
		const auto  pos = a_actor.GetPosition();
		const float dx = a_source.x - pos.x;
		const float dy = a_source.y - pos.y;
		const float delta = std::atan2(dx, dy) - a_actor.GetAngleZ();
		constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;
		float           turns = delta / kTwoPi;
		turns -= std::floor(turns);
		return turns;
	}

	bool PlayStagger(RE::Actor* a_actor, const RE::NiPoint3& a_source)
	{
		const auto* strings = RE::FixedStrings::GetSingleton();
		if (!strings || strings->staggerMagnitude.empty() || strings->staggerDirection.empty()) {
			return false;
		}

		static const RE::BSFixedString staggerStart{ "staggerStart" };
		a_actor->SetGraphVariableFloat(strings->staggerDirection, StaggerDirection(*a_actor, a_source));
		a_actor->SetGraphVariableFloat(strings->staggerMagnitude, kStaggerMagnitude);
		a_actor->NotifyAnimationGraph(staggerStart);
		return true;
	}

	using SetGraphVariableFloat_t = bool(RE::BShkbAnimationGraph*, const RE::BSFixedString&, float);
	using KnockExplosion_t = void(RE::AIProcess*, RE::Actor*, const RE::NiPoint3&, float);

	SetGraphVariableFloat_t* g_setGraphVariableFloat = nullptr;
	KnockExplosion_t*        g_knockExplosion = nullptr;

	bool SetGraphVariableFloat(RE::BShkbAnimationGraph* a_graph, const RE::BSFixedString& a_name, float a_value)
	{
		if (a_value >= kKnockdownMagnitude && a_graph && a_graph->holder) {
			const auto* magnitude = StaggerMagnitudeName();
			if (magnitude && a_name == *magnitude && ShouldConvert(a_graph->holder)) {
				SKSE::log::debug(
					"Converted knockdown magnitude {:.3f} to {:.2f}",
					a_value,
					kStaggerMagnitude);
				a_value = kStaggerMagnitude;
			}
		}

		return g_setGraphVariableFloat(a_graph, a_name, a_value);
	}

	void KnockExplosion(RE::AIProcess* a_process, RE::Actor* a_actor, const RE::NiPoint3& a_location, float a_magnitude)
	{
		if (ShouldConvert(a_actor)) {
			if (PlayStagger(a_actor, a_location)) {
				SKSE::log::debug("Converted KnockExplosion to stagger");
				return;
			}

			SKSE::log::warn("Could not play replacement stagger; using KnockExplosion");
		}

		g_knockExplosion(a_process, a_actor, a_location, a_magnitude);
	}
}

namespace KnockdownImmunity
{
	void Install()
	{
		Detour<SetGraphVariableFloat_t>(
			RELOCATION_ID(62709, 63608).address(),
			SetGraphVariableFloat,
			g_setGraphVariableFloat,
			"BShkbAnimationGraph::SetGraphVariableFloat"sv);
		Detour<KnockExplosion_t>(
			RELOCATION_ID(38858, 39895).address(),
			KnockExplosion,
			g_knockExplosion,
			"AIProcess::KnockExplosion"sv);
	}
}
