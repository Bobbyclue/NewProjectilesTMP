#include "TriggerFunctions.h"
#include "JsonUtils.h"
#include "RuntimeData.h"

namespace Emitters
{
	struct SpeedData
	{
		enum class SpeedChangeTypes : uint32_t
		{
			Linear,
			Quadratic,
			Exponential
		} type;

		float time;
	};

	struct FunctionData
	{
		enum class Type : uint32_t
		{
			AccelerateToMaxSpeed,  // accelerate until max speed, during given time
			TriggerFunctions       // call triggers in NewProjsType
		};

		std::variant<SpeedData, TriggerFunctions::Functions> data;

		FunctionData(const std::string& filename, const Json::Value& function)
		{
			Type type = JsonUtils::read_enum<Type>(function, "type");
			switch (type) {
			case Type::AccelerateToMaxSpeed:
				data = SpeedData{ JsonUtils::read_enum<SpeedData::SpeedChangeTypes>(function, "speedType"),
					JsonUtils::getFloat(function, "time") };
				break;
			case Type::TriggerFunctions:
				data = TriggerFunctions::Functions(filename, function["TriggerFunctions"]);
				break;
			default:
				assert(false);
			}
		}

		Type get_type() const { return static_cast<Type>(data.index()); }
	};

	struct Data
	{
		std::vector<FunctionData> functions;
		float interval;
		uint32_t limited: 1;
		uint32_t count: 30;
		uint32_t destroy_after: 1;
	};

	struct Storage
	{
		static void clear_keys() { keys.clear(); }
		static void clear()
		{
			clear_keys();
			data_static.clear();
		}

		static void init(const std::string& filename, const Json::Value& HomingData)
		{
			for (auto& key : HomingData.getMemberNames()) {
				read_json_entry(filename, key, HomingData[key]);
			}
		}

		static void init_keys(const std::string& filename, const Json::Value& HomingData)
		{
			for (auto& key : HomingData.getMemberNames()) {
				read_json_entry_keys(filename, key, HomingData[key]);
			}
		}

		static const auto& get_data(uint32_t ind) { return data_static[ind - 1]; }

		static uint32_t get_key_ind(const std::string& filename, const std::string& key) { return keys.get(filename, key); }

	private:
		static void read_json_entry(const std::string& filename, const std::string& key, const Json::Value& item)
		{
			[[maybe_unused]] uint32_t ind = keys.get(filename, key);
			assert(ind == data_static.size() + 1);

			const auto& functions = item["functions"];

			data_static.emplace_back(std::vector<FunctionData>(), JsonUtils::getFloat(item, "interval"),
				JsonUtils::mb_read_field<false>(item, "limited"), JsonUtils::mb_read_field<1u>(item, "count"),
				JsonUtils::mb_read_field<false>(item, "destroyAfter"));

			auto& new_functions = data_static.back().functions;
			for (size_t i = 0; i < functions.size(); i++) {
				const auto& function = functions[(int)i];

				new_functions.emplace_back(filename, function);
			}
		}

		static void read_json_entry_keys(const std::string& filename, const std::string& key, const Json::Value&)
		{
			keys.add(filename, key);
		}

		static inline JsonUtils::KeysMap keys;
		static inline std::vector<Data> data_static;
	};

	uint32_t get_key_ind(const std::string& filename, const std::string& key) { return Storage::get_key_ind(filename, key); }

	void clear() { Storage::clear(); }
	void clear_keys() { Storage::clear_keys(); }

	void init(const std::string& filename, const Json::Value& json_root)
	{
		if (json_root.isMember("EmittersData")) {
			Storage::init(filename, json_root["EmittersData"]);
		}
	}

	void init_keys(const std::string& filename, const Json::Value& json_root)
	{
		if (json_root.isMember("EmittersData")) {
			Storage::init_keys(filename, json_root["EmittersData"]);
		}
	}

	void set_emitter_ind(RE::Projectile* proj, uint32_t ind) { ::set_emitter_ind(proj, ind); }
	uint32_t get_emitter_ind(RE::Projectile* proj) { return ::get_emitter_ind(proj); }
	bool is_emitter(RE::Projectile* proj) { return get_emitter_ind(proj) != 0; }
	void disable_emitter(RE::Projectile* proj) { set_emitter_ind(proj, 0); }

	void disable(RE::Projectile* proj)
	{
		if (proj && proj->IsMissileProjectile()) {
			disable_emitter(proj);
		}
	}

	void apply(RE::Projectile* proj, uint32_t ind)
	{
		if (proj->IsMissileProjectile()) {
			assert(ind > 0);
			set_emitter_ind(proj, ind);
			auto emitter_ind = get_emitter_ind(proj);
			auto& data = Storage::get_data(emitter_ind);
			if (data.limited) {
				set_emitter_rest(proj, data.count);
			}
		}
	}

	void onUpdate(RE::Projectile* proj, float dtime)
	{
		auto emitter_ind = get_emitter_ind(proj);

		auto& data = Storage::get_data(emitter_ind);

		if (proj->livingTime < data.interval)
			return;

		if (data.limited && get_emitter_rest(proj) == 0)
			return;

		proj->livingTime = 0.000001f;

		for (const auto& function : data.functions) {
			switch (function.get_type()) {
			case FunctionData::Type::TriggerFunctions:
				std::get<TriggerFunctions::Functions>(function.data).call(proj);
				break;
			case FunctionData::Type::AccelerateToMaxSpeed:
				{
					// TODO: fix
					// y = a*e^bx, a = M/X, b = ln X / N
					//constexpr float X = 70.0f;
					//constexpr float LN_X = 4.248495242049359f;  // ln 70
					//float b = LN_X / function.args.time;
					//float cur_speed = proj->linearVelocity.Length();
					//float add_to_speed = exp(b * dtime);
					//float new_speed = cur_speed * add_to_speed;
					//proj->linearVelocity *= (new_speed / cur_speed);

					float max_speed = FenixUtils::Projectile__GetSpeed(proj);
					float cur_speed = proj->linearVelocity.Length();
					if (cur_speed < max_speed) {
						auto& function_speed = std::get<SpeedData>(function.data);
						float TOTAL_TIME = function_speed.time;
						float dspeed = 0;
						switch (function_speed.type) {
						case SpeedData::SpeedChangeTypes::Linear:
							dspeed = max_speed / TOTAL_TIME * dtime;
							break;
						case SpeedData::SpeedChangeTypes::Quadratic:
							dspeed = 2 * std::sqrt(cur_speed * max_speed) * TOTAL_TIME * dtime;
							break;
						case SpeedData::SpeedChangeTypes::Exponential:
							dspeed = cur_speed * std::log(max_speed) / TOTAL_TIME * dtime;
							break;
						}

						float new_speed = cur_speed + dspeed;
						proj->linearVelocity *= (new_speed / cur_speed);
					}
				}
				break;
			default:
				break;
			}
		}

		if (data.limited) {
			auto rest = get_emitter_rest(proj);
			set_emitter_rest(proj, rest - 1);
			if (rest == 1) {
				if (data.destroy_after) {
					// proj->Kill();				// Function does not exist, no clue what it did
					proj->Disable();				// Temp "fix" for AE port
					proj->SetDelete(true);
				} else {
					disable_emitter(proj);
				}
			}
		}
	}

	namespace Hooks
	{
		class EmitterHook
		{
		public:
			static void Hook()
			{
				_CheckExplosion = SKSE::GetTrampoline().write_call<5>(RELOCATION_ID(42852, 44027).address() + 0x6E1,
					CheckExplosion);  // SkyrimSE.exe+745450
				_AddImpact = SKSE::GetTrampoline().write_call<5>(RELOCATION_ID(42547, 43710).address() + 0x50,
					AddImpact);  // SkyrimSE.exe+732456 -- disable on hit
				_BSSoundHandle__ClearFollowedObject = SKSE::GetTrampoline().write_call<5>(RELOCATION_ID(42930, 44110).address() + 0x21,
					BSSoundHandle__ClearFollowedObject);  // SkyrimSE.exe+74BC21 -- disable on kill
			}

		private:
			static RE::Explosion* CheckExplosion(RE::MissileProjectile* proj, float dtime)
			{
				if (is_emitter(proj)) {
					onUpdate(proj, dtime);
				}

				return _CheckExplosion(proj, dtime);
			}

			static void* AddImpact(RE::Projectile* proj, RE::TESObjectREFR* a2, RE::NiPoint3* a3, RE::NiPoint3* a_velocity,
				RE::hkpCollidable* a_collidable, uint32_t a6, char a7)
			{
				auto ans = _AddImpact(proj, a2, a3, a_velocity, a_collidable, a6, a7);
				if (is_emitter(proj)) {
					disable_emitter(proj);
				}
				return ans;
			}
			static void BSSoundHandle__ClearFollowedObject(char* sound)
			{
				_BSSoundHandle__ClearFollowedObject(sound);
				auto proj = reinterpret_cast<RE::Projectile*>(sound - 0x128);
				if (is_emitter(proj)) {
					disable_emitter(proj);
				}
			}

			static inline REL::Relocation<decltype(CheckExplosion)> _CheckExplosion;
			static inline REL::Relocation<decltype(AddImpact)> _AddImpact;
			static inline REL::Relocation<decltype(BSSoundHandle__ClearFollowedObject)> _BSSoundHandle__ClearFollowedObject;
		};
	}

	void install() { Hooks::EmitterHook::Hook(); }
}
