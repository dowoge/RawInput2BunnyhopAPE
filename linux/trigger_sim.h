

#pragma once

#include <cmath>
#include <climits>

static const int   PRED_MAX_EVENTS   = 512;
static const int   PRED_RING_SIZE     = 256;
static const int   PRED_SERVICE_CAP    = 1024;
static const float PRED_NEVER          = -1e9f;

static const int FL_ONGROUND     = 1 << 0;
static const int FL_CLIENT_BIT   = 1 << 8;
static const int FL_BASEVELOCITY = 1 << 24;

enum PredClass {
	PC_NONE, PC_TELEPORT, PC_PUSH, PC_GRAVITY, PC_MULTIPLE, PC_ONCE,
	PC_RELAY, PC_COUNTER, PC_BRANCH, PC_CASE
};

enum PredInput {
	IN_NONE, IN_ENABLE, IN_DISABLE, IN_TOGGLE, IN_TRIGGER, IN_ENABLEREFIRE, IN_CANCELPENDING,
	IN_CNT_ADD, IN_CNT_SUB, IN_CNT_MUL, IN_CNT_DIV, IN_CNT_SETVALUE, IN_CNT_SETVALUENOFIRE,
	IN_CNT_SETHITMAX, IN_CNT_SETHITMIN, IN_CNT_GETVALUE,
	IN_BR_SETVALUE, IN_BR_SETVALUETEST, IN_BR_TOGGLE, IN_BR_TOGGLETEST, IN_BR_TEST,
	IN_CASE_INVALUE
};

enum PredOutput {
	OUT_ONSTARTTOUCH, OUT_ONSTARTTOUCHALL, OUT_ONENDTOUCH, OUT_ONENDTOUCHALL,
	OUT_ONTOUCHING, OUT_ONNOTTOUCHING, OUT_ONTRIGGER,
	OUT_OUTVALUE, OUT_ONGETVALUE, OUT_ONHITMIN, OUT_ONHITMAX,
	OUT_ONTRUE, OUT_ONFALSE, OUT_ONDEFAULT, OUT_ONCASE0
};

enum PredPlayerKey { PPK_NONE, PPK_BASEVELOCITY, PPK_GRAVITY, PPK_TARGETNAME };

static const int PRED_TARGET_ACTIVATOR = -2;

struct PredAction {
	int   output;
	int   target;
	int   input;
	float param;
	bool  hasParam;
	float delay;
	int   timesToFire;

	int         playerKey = PPK_NONE;
	float       playerVec[3] = {0,0,0};
	std::string playerName;
};

enum PredFilterKind { FK_NONE, FK_NAME, FK_CLASS, FK_MULTI, FK_UNSUPPORTED };

struct PredFilter {
	PredFilterKind kind = FK_UNSUPPORTED;
	bool negated = false;
	std::string param;
	std::vector<int> subs;
	bool multiAnd = true;
};

struct PredSimEnt {
	PredClass cls = PC_NONE;
	int   modelIndex = -1;
	float modelShift[3] = {0,0,0};
	int   spawnflags = 0;
	int   filterIndex = -1;
	int   actionStart = 0;
	int   actionCount = 0;

	float speed = 0.0f;
	float pushDir[3] = {0,0,0};
	float gravity = 1.0f;
	float wait = 0.0f;

	bool  destValid = false;
	float destOrigin[3] = {0,0,0};
	float destAngles[3] = {0,0,0};
	bool  snapAngles = false;
	bool  landmarkValid = false;
	float landmarkOrigin[3] = {0,0,0};

	bool  initialDisabled = false;
	float initialCounter = 0.0f;
	float counterMin = 0.0f;
	float counterMax = 0.0f;
	bool  initialBranch = false;

	int   caseCount = 0;
	float caseValue[16] = {0};
};

struct PredEntState {
	uint8_t disabled = 0;
	uint8_t playerInside = 0;
	uint8_t startTouchFired = 0;
	uint8_t hitMin = 0, hitMax = 0, branchValue = 0, waitForRefire = 0;
	float   counterValue = 0.0f;
	float   multiWaitUntil = PRED_NEVER;
};

struct PredEvent {
	float fireTime;
	int   target;
	int   input;
	float value;
	int   action;
};

struct PredWorld {
	std::vector<PredEntState> ents;
	std::vector<int>          actionTimesLeft;
	std::vector<PredEvent>    events;
	std::string               playerName;
};

static bool PredParseVec3(const char* s, float out[3])
{
	out[0] = out[1] = out[2] = 0.0f;
	return sscanf(s, "%f %f %f", &out[0], &out[1], &out[2]) >= 1;
}

static PredClass PredClassify(const char* cls)
{
	if (!strcmp(cls, "trigger_teleport")) return PC_TELEPORT;
	if (!strcmp(cls, "trigger_push"))     return PC_PUSH;
	if (!strcmp(cls, "trigger_gravity"))  return PC_GRAVITY;
	if (!strcmp(cls, "trigger_multiple")) return PC_MULTIPLE;
	if (!strcmp(cls, "trigger_once"))     return PC_ONCE;
	if (!strcmp(cls, "logic_relay"))      return PC_RELAY;
	if (!strcmp(cls, "math_counter"))     return PC_COUNTER;
	if (!strcmp(cls, "logic_branch"))     return PC_BRANCH;
	if (!strcmp(cls, "logic_case"))       return PC_CASE;
	return PC_NONE;
}

static bool PredIsTriggerClass(PredClass c)
{
	return c == PC_TELEPORT || c == PC_PUSH || c == PC_GRAVITY || c == PC_MULTIPLE || c == PC_ONCE;
}

static int PredOutputId(const char* name)
{
	if (!strcasecmp(name, "OnStartTouch"))    return OUT_ONSTARTTOUCH;
	if (!strcasecmp(name, "OnStartTouchAll")) return OUT_ONSTARTTOUCHALL;
	if (!strcasecmp(name, "OnEndTouch"))      return OUT_ONENDTOUCH;
	if (!strcasecmp(name, "OnEndTouchAll"))   return OUT_ONENDTOUCHALL;
	if (!strcasecmp(name, "OnTouching"))      return OUT_ONTOUCHING;
	if (!strcasecmp(name, "OnNotTouching"))   return OUT_ONNOTTOUCHING;
	if (!strcasecmp(name, "OnTrigger"))       return OUT_ONTRIGGER;
	if (!strcasecmp(name, "OutValue"))        return OUT_OUTVALUE;
	if (!strcasecmp(name, "OnGetValue"))      return OUT_ONGETVALUE;
	if (!strcasecmp(name, "OnHitMin"))        return OUT_ONHITMIN;
	if (!strcasecmp(name, "OnHitMax"))        return OUT_ONHITMAX;
	if (!strcasecmp(name, "OnTrue"))          return OUT_ONTRUE;
	if (!strcasecmp(name, "OnFalse"))         return OUT_ONFALSE;
	if (!strcasecmp(name, "OnDefault"))       return OUT_ONDEFAULT;
	if (!strncasecmp(name, "OnCase", 6)) {
		int n = atoi(name + 6);
		if (n >= 1 && n <= 16) return OUT_ONCASE0 + (n - 1);
	}
	return -1;
}

static int PredInputId(PredClass targetCls, const char* name)
{
	if (!strcasecmp(name, "Enable"))  return IN_ENABLE;
	if (!strcasecmp(name, "Disable")) return IN_DISABLE;
	if (!strcasecmp(name, "Toggle"))  return IN_TOGGLE;
	switch (targetCls) {
	case PC_RELAY:
		if (!strcasecmp(name, "Trigger"))       return IN_TRIGGER;
		if (!strcasecmp(name, "EnableRefire"))  return IN_ENABLEREFIRE;
		if (!strcasecmp(name, "CancelPending")) return IN_CANCELPENDING;
		break;
	case PC_COUNTER:
		if (!strcasecmp(name, "Add"))            return IN_CNT_ADD;
		if (!strcasecmp(name, "Subtract"))       return IN_CNT_SUB;
		if (!strcasecmp(name, "Multiply"))       return IN_CNT_MUL;
		if (!strcasecmp(name, "Divide"))         return IN_CNT_DIV;
		if (!strcasecmp(name, "SetValue"))       return IN_CNT_SETVALUE;
		if (!strcasecmp(name, "SetValueNoFire")) return IN_CNT_SETVALUENOFIRE;
		if (!strcasecmp(name, "SetHitMax"))      return IN_CNT_SETHITMAX;
		if (!strcasecmp(name, "SetHitMin"))      return IN_CNT_SETHITMIN;
		if (!strcasecmp(name, "GetValue"))       return IN_CNT_GETVALUE;
		break;
	case PC_BRANCH:
		if (!strcasecmp(name, "SetValue"))     return IN_BR_SETVALUE;
		if (!strcasecmp(name, "SetValueTest")) return IN_BR_SETVALUETEST;
		if (!strcasecmp(name, "ToggleTest"))   return IN_BR_TOGGLETEST;
		if (!strcasecmp(name, "Test"))         return IN_BR_TEST;
		break;
	case PC_CASE:
		if (!strcasecmp(name, "InValue")) return IN_CASE_INVALUE;
		break;
	default:
		break;
	}
	return IN_NONE;
}

static void PredSplitOutput(const char* value, std::vector<std::string>& fields)
{
	fields.clear();
	std::string cur;
	for (const char* p = value; ; ++p) {
		if (*p == ',' || *p == 0x1b || *p == 0) {
			fields.push_back(cur);
			cur.clear();
			if (*p == 0) break;
		} else {
			cur.push_back(*p);
		}
	}
}

static void PredAngleVectorsForward(const float ang[3], float fwd[3])
{
	const float d2r = 3.14159265358979323846f / 180.0f;
	float sp = sinf(ang[0] * d2r), cp = cosf(ang[0] * d2r);
	float sy = sinf(ang[1] * d2r), cy = cosf(ang[1] * d2r);
	fwd[0] = cp * cy;
	fwd[1] = cp * sy;
	fwd[2] = -sp;
}

static int PredApplySpawnFixup(PredClass cls, int sf)
{

	if (cls == PC_TELEPORT) return sf;
	if (sf & 0x810) sf |= 0x02;
	if (sf & 0x20)  sf |= 0x01;
	if (sf & 0x200) sf |= 0x01;
	return sf;
}

static bool PredFilterNegated(const char* v)
{

	return v[0] == '1' || strncasecmp(v, "Disallow", 8) == 0;
}

static void PredBuildFilters(PredMap& map, std::vector<int>& filterEntity)
{
	for (int i = 0; i < (int)map.entities.size(); ++i) {
		const char* c = map.entities[i].Get("classname");
		if (strncmp(c, "filter_", 7) != 0) continue;
		const PredMapEntity& e = map.entities[i];
		PredFilter f;
		f.negated = PredFilterNegated(e.Get("Negated", "0"));
		if (!strcmp(c, "filter_activator_name"))       { f.kind = FK_NAME;  f.param = e.Get("filtername"); }
		else if (!strcmp(c, "filter_activator_class")) { f.kind = FK_CLASS; f.param = e.Get("filterclass"); }
		else if (!strcmp(c, "filter_multi"))           { f.kind = FK_MULTI; f.multiAnd = atoi(e.Get("FilterType", "0")) == 0; }
		else                                             f.kind = FK_UNSUPPORTED;
		filterEntity.push_back(i);
		map.filters.push_back(f);
	}

	for (int fi = 0; fi < (int)map.filters.size(); ++fi) {
		if (map.filters[fi].kind != FK_MULTI) continue;
		const PredMapEntity& e = map.entities[filterEntity[fi]];
		for (int k = 1; k <= 10; ++k) {
			char key[16];
			snprintf(key, sizeof(key), "Filter%02d", k);
			const char* name = e.Get(key);
			if (!name[0]) continue;
			int found = -1;
			for (int j = 0; j < (int)map.filters.size(); ++j)
				if (!strcmp(map.entities[filterEntity[j]].Get("targetname"), name)) { found = j; break; }
			if (found >= 0) map.filters[fi].subs.push_back(found);
			else            map.filters[fi].kind = FK_UNSUPPORTED;
		}
	}
}

static bool PredWildcardMatch(const char* pat, const char* s)
{
	size_t n = strlen(pat);
	if (n && pat[n - 1] == '*') return strncasecmp(pat, s, n - 1) == 0;
	return strcasecmp(pat, s) == 0;
}

static bool PredFilterPasses(const PredMap& map, int fi, const char* playerName)
{
	if (fi < 0 || fi >= (int)map.filters.size()) return true;
	const PredFilter& f = map.filters[fi];
	bool impl;
	switch (f.kind) {
	case FK_NAME:  impl = PredWildcardMatch(f.param.c_str(), playerName); break;
	case FK_CLASS: impl = PredWildcardMatch(f.param.c_str(), "player"); break;
	case FK_MULTI: {
		if (f.subs.empty()) { impl = true; break; }
		impl = f.multiAnd;
		for (int s : f.subs) {
			bool p = PredFilterPasses(map, s, playerName);
			impl = f.multiAnd ? (impl && p) : (impl || p);
		}
		break; }
	default:
		return false;
	}
	return f.negated ? !impl : impl;
}

static void PredBuildTriggers(PredMap& map)
{
	std::vector<int> simEntity;
	std::vector<int> filterEntity;
	map.sim.clear();
	map.actions.clear();
	map.filters.clear();
	PredBuildFilters(map, filterEntity);

	for (int i = 0; i < (int)map.entities.size(); ++i) {
		PredClass c = PredClassify(map.entities[i].Get("classname"));
		if (c == PC_NONE) continue;
		PredSimEnt e;
		e.cls = c;
		simEntity.push_back(i);
		map.sim.push_back(e);
	}

	auto findSimByName = [&](const char* name, std::vector<int>& out) {
		out.clear();
		if (!name || !name[0]) return;
		for (int s = 0; s < (int)map.sim.size(); ++s)
			if (!strcmp(map.entities[simEntity[s]].Get("targetname"), name)) out.push_back(s);
	};
	auto findFilterByName = [&](const char* name) -> int {
		if (!name || !name[0]) return -1;
		for (int j = 0; j < (int)map.filters.size(); ++j)
			if (!strcmp(map.entities[filterEntity[j]].Get("targetname"), name)) return j;
		return -1;
	};
	auto findPoint = [&](const char* name, float origin[3], float angles[3]) -> bool {
		if (!name || !name[0]) return false;
		for (const auto& ent : map.entities) {
			if (strcmp(ent.Get("targetname"), name) != 0) continue;
			PredParseVec3(ent.Get("origin"), origin);
			PredParseVec3(ent.Get("angles"), angles);
			return true;
		}
		return false;
	};

	std::vector<std::string> fields;
	for (int s = 0; s < (int)map.sim.size(); ++s) {
		PredSimEnt& e = map.sim[s];
		const PredMapEntity& ent = map.entities[simEntity[s]];

		int sf = atoi(ent.Get("spawnflags", "0"));
		e.spawnflags = PredApplySpawnFixup(e.cls, sf);
		e.initialDisabled = atoi(ent.Get("StartDisabled", "0")) != 0;

		const char* model = ent.Get("model");
		e.modelIndex = model[0] == '*' ? atoi(model + 1) : -1;
		PredParseVec3(ent.Get("origin", "0 0 0"), e.modelShift);
		if (PredIsTriggerClass(e.cls)) e.filterIndex = findFilterByName(ent.Get("filtername"));

		bool skip = false;
		switch (e.cls) {
		case PC_TELEPORT: {
			float o[3], a[3];
			e.destValid = findPoint(ent.Get("target"), o, a);
			if (e.destValid) { memcpy(e.destOrigin, o, sizeof o); memcpy(e.destAngles, a, sizeof a); }
			e.landmarkValid = findPoint(ent.Get("landmark"), o, a);
			if (e.landmarkValid) memcpy(e.landmarkOrigin, o, sizeof o);
			if (!e.destValid) skip = true;

			e.snapAngles = e.destValid && !e.landmarkValid && !(e.spawnflags & 0x20);
			break; }
		case PC_PUSH: {
			e.speed = (float)atof(ent.Get("speed", "0"));
			if (e.speed == 0.0f) e.speed = 100.0f;
			float ang[3];
			PredParseVec3(ent.Get("pushdir", "0 0 0"), ang);
			PredAngleVectorsForward(ang, e.pushDir);
			break; }
		case PC_GRAVITY:
			e.gravity = (float)atof(ent.Get("gravity", "1"));
			break;
		case PC_MULTIPLE:
			e.wait = (float)atof(ent.Get("wait", "0"));
			if (e.wait == 0.0f) e.wait = 0.2f;
			break;
		case PC_ONCE:
			e.wait = -1.0f;
			break;
		case PC_COUNTER:
			e.initialCounter = (float)atof(ent.Get("startvalue", "0"));
			e.counterMin = (float)atof(ent.Get("min", "0"));
			e.counterMax = (float)atof(ent.Get("max", "0"));
			break;
		case PC_BRANCH:
			e.initialBranch = atoi(ent.Get("InitialValue", "0")) != 0;
			break;
		case PC_CASE:
			for (int k = 0; k < 16; ++k) {
				char key[16];
				snprintf(key, sizeof(key), "Case%02d", k + 1);
				const char* v = ent.Get(key);
				if (v[0]) { e.caseValue[k] = (float)atof(v); e.caseCount = k + 1; }
			}
			break;
		default:
			break;
		}
		if (PredIsTriggerClass(e.cls) && e.modelIndex < 0) skip = true;
		if (skip) { e.cls = PC_NONE; continue; }

		e.actionStart = (int)map.actions.size();
		for (const auto& kv : ent.kv) {
			int outId = PredOutputId(kv.key.c_str());
			if (outId < 0) continue;
			PredSplitOutput(kv.value.c_str(), fields);
			if (fields.size() < 2) continue;

			if (strcasecmp(fields[0].c_str(), "!activator") == 0) {
				if (strcasecmp(fields[1].c_str(), "AddOutput") != 0) continue;
				if (fields.size() < 3) continue;

				const std::string& kvs = fields[2];
				size_t sp = kvs.find(' ');
				std::string key = kvs.substr(0, sp);
				std::string val = sp == std::string::npos ? "" : kvs.substr(sp + 1);
				while (!val.empty() && val[0] == ' ') val.erase(val.begin());

				PredAction a;
				a.output = outId;
				a.target = PRED_TARGET_ACTIVATOR;
				a.input = IN_NONE;
				a.param = 0.0f;
				a.hasParam = false;
				a.delay = fields.size() > 3 ? (float)atof(fields[3].c_str()) : 0.0f;
				a.timesToFire = fields.size() > 4 && !fields[4].empty() ? atoi(fields[4].c_str()) : -1;

				if (strcasecmp(key.c_str(), "basevelocity") == 0) {
					a.playerKey = PPK_BASEVELOCITY;
					PredParseVec3(val.c_str(), a.playerVec);
				} else if (strcasecmp(key.c_str(), "gravity") == 0) {
					a.playerKey = PPK_GRAVITY;
					a.playerVec[0] = (float)atof(val.c_str());
				} else if (strcasecmp(key.c_str(), "targetname") == 0) {
					a.playerKey = PPK_TARGETNAME;
					a.playerName = val;
				} else {
					continue;
				}

				map.actions.push_back(a);
				continue;
			}

			std::vector<int> targets;
			findSimByName(fields[0].c_str(), targets);
			if (targets.empty()) continue;
			for (int t : targets) {
				int inId = PredInputId(map.sim[t].cls, fields[1].c_str());
				if (inId == IN_NONE) continue;
				PredAction a;
				a.output = outId;
				a.target = t;
				a.input = inId;
				a.hasParam = fields.size() > 2 && !fields[2].empty();
				a.param = a.hasParam ? (float)atof(fields[2].c_str()) : 0.0f;
				a.delay = fields.size() > 3 ? (float)atof(fields[3].c_str()) : 0.0f;
				a.timesToFire = fields.size() > 4 && !fields[4].empty() ? atoi(fields[4].c_str()) : -1;
				map.actions.push_back(a);
			}
		}
		e.actionCount = (int)map.actions.size() - e.actionStart;
	}

	for (const PredSimEnt& e : map.sim) {
		if (e.cls != PC_NONE) map.stats.active++;
	}
}

static void PredEnqueue(PredWorld& w, float fireTime, int target, int input, float value, int action = -1)
{
	if ((int)w.events.size() >= PRED_MAX_EVENTS) return;
	PredEvent ev{fireTime, target, input, value, action};
	auto it = w.events.begin();
	while (it != w.events.end() && it->fireTime <= fireTime) ++it;
	w.events.insert(it, ev);
}

static void PredFireOutput(PredMap& map, PredWorld& w, int entIdx, int outId, float value, float curtime)
{
	const PredSimEnt& e = map.sim[entIdx];
	for (int k = 0; k < e.actionCount; ++k) {
		int ai = e.actionStart + k;
		const PredAction& a = map.actions[ai];
		if (a.output != outId) continue;
		if (a.timesToFire != -1 && w.actionTimesLeft[ai] == 0) continue;
		float v = a.hasParam ? a.param : value;
		PredEnqueue(w, curtime + a.delay, a.target, a.input, v, ai);
		if (a.timesToFire != -1 && w.actionTimesLeft[ai] > 0) w.actionTimesLeft[ai]--;
	}
}

static float PredMaxDelay(const PredMap& map, int entIdx, int outId)
{
	const PredSimEnt& e = map.sim[entIdx];
	float m = 0.0f;
	for (int k = 0; k < e.actionCount; ++k) {
		const PredAction& a = map.actions[e.actionStart + k];
		if (a.output == outId && a.delay > m) m = a.delay;
	}
	return m;
}

static void PredCounterUpdate(PredMap& map, PredWorld& w, int entIdx, float value, float curtime)
{
	const PredSimEnt& e = map.sim[entIdx];
	PredEntState& st = w.ents[entIdx];
	if (e.counterMin != 0.0f || e.counterMax != 0.0f) {
		if (value >= e.counterMax) { if (!st.hitMax) { st.hitMax = 1; PredFireOutput(map, w, entIdx, OUT_ONHITMAX, value, curtime); } }
		else st.hitMax = 0;
		if (value <= e.counterMin) { if (!st.hitMin) { st.hitMin = 1; PredFireOutput(map, w, entIdx, OUT_ONHITMIN, value, curtime); } }
		else st.hitMin = 0;
		value = fminf(fmaxf(value, e.counterMin), e.counterMax);
	}
	st.counterValue = value;
	PredFireOutput(map, w, entIdx, OUT_OUTVALUE, value, curtime);
}

static void PredAcceptInput(PredMap& map, PredWorld& w, int entIdx, int input, float value, float curtime)
{
	const PredSimEnt& e = map.sim[entIdx];
	PredEntState& st = w.ents[entIdx];
	switch (input) {
	case IN_ENABLE:  st.disabled = 0; break;
	case IN_DISABLE: st.disabled = 1; break;
	case IN_TOGGLE:  st.disabled ^= 1; break;

	case IN_TRIGGER:
		if (st.disabled || st.waitForRefire) break;
		PredFireOutput(map, w, entIdx, OUT_ONTRIGGER, value, curtime);
		if (e.spawnflags & 0x001) st.disabled = 1;
		else if (!(e.spawnflags & 0x002)) {
			st.waitForRefire = 1;
			PredEnqueue(w, curtime + PredMaxDelay(map, entIdx, OUT_ONTRIGGER) + 0.001f, entIdx, IN_ENABLEREFIRE, 0.0f);
		}
		break;
	case IN_ENABLEREFIRE:  st.waitForRefire = 0; break;
	case IN_CANCELPENDING: st.waitForRefire = 0; break;

	case IN_CNT_ADD:            if (!st.disabled) PredCounterUpdate(map, w, entIdx, st.counterValue + value, curtime); break;
	case IN_CNT_SUB:            if (!st.disabled) PredCounterUpdate(map, w, entIdx, st.counterValue - value, curtime); break;
	case IN_CNT_MUL:            if (!st.disabled) PredCounterUpdate(map, w, entIdx, st.counterValue * value, curtime); break;
	case IN_CNT_DIV:            if (!st.disabled && value != 0.0f) PredCounterUpdate(map, w, entIdx, st.counterValue / value, curtime); break;
	case IN_CNT_SETVALUE:       if (!st.disabled) PredCounterUpdate(map, w, entIdx, value, curtime); break;
	case IN_CNT_SETVALUENOFIRE: st.counterValue = fminf(fmaxf(value, e.counterMin), e.counterMax); break;
	case IN_CNT_SETHITMAX:      break;
	case IN_CNT_SETHITMIN:      break;
	case IN_CNT_GETVALUE:       PredFireOutput(map, w, entIdx, OUT_ONGETVALUE, st.counterValue, curtime); break;

	case IN_BR_SETVALUE:     st.branchValue = value != 0.0f; break;
	case IN_BR_SETVALUETEST: st.branchValue = value != 0.0f; PredFireOutput(map, w, entIdx, st.branchValue ? OUT_ONTRUE : OUT_ONFALSE, value, curtime); break;
	case IN_BR_TOGGLE:       st.branchValue ^= 1; break;
	case IN_BR_TOGGLETEST:   st.branchValue ^= 1; PredFireOutput(map, w, entIdx, st.branchValue ? OUT_ONTRUE : OUT_ONFALSE, value, curtime); break;
	case IN_BR_TEST:         PredFireOutput(map, w, entIdx, st.branchValue ? OUT_ONTRUE : OUT_ONFALSE, value, curtime); break;

	case IN_CASE_INVALUE: {
		for (int k = 0; k < e.caseCount; ++k)
			if (e.caseValue[k] == value) { PredFireOutput(map, w, entIdx, OUT_ONCASE0 + k, value, curtime); return; }
		PredFireOutput(map, w, entIdx, OUT_ONDEFAULT, value, curtime);
		break; }
	default:
		break;
	}
}

static void PredApplyToPlayer(const PredAction& a, PredWorld& w, PredPlayerIO& io)
{
	switch (a.playerKey) {

	case PPK_BASEVELOCITY:

		io.baseVelocity[0] = a.playerVec[0];
		io.baseVelocity[1] = a.playerVec[1];
		io.baseVelocity[2] = a.playerVec[2];
		break;

	case PPK_GRAVITY:
		*io.gravity = a.playerVec[0];
		break;

	case PPK_TARGETNAME:
		w.playerName = a.playerName;
		break;

	default:
		break;
	}
}

static void PredServiceEvents(PredMap& map, PredWorld& w, float curtime, PredPlayerIO& io)
{
	for (int guard = 0; guard < PRED_SERVICE_CAP; ++guard) {
		if (w.events.empty() || w.events.front().fireTime > curtime) return;
		PredEvent ev = w.events.front();
		w.events.erase(w.events.begin());

		if (ev.target == PRED_TARGET_ACTIVATOR) {
			if (ev.action >= 0 && ev.action < (int)map.actions.size())
				PredApplyToPlayer(map.actions[ev.action], w, io);
			continue;
		}

		PredAcceptInput(map, w, ev.target, ev.input, ev.value, curtime);
	}
}

static void PredEffectTeleport(const PredSimEnt& e, PredPlayerIO& io)
{
	if (!e.destValid) return;
	float pos[3];
	if (e.landmarkValid) {
		for (int c = 0; c < 3; ++c) pos[c] = e.destOrigin[c] + (io.origin[c] - e.landmarkOrigin[c]);
	} else {
		pos[0] = e.destOrigin[0];
		pos[1] = e.destOrigin[1];
		pos[2] = e.destOrigin[2] - io.mins[2];
	}
	io.origin[0] = pos[0];
	io.origin[1] = pos[1];
	io.origin[2] = pos[2];
	*io.flags &= ~FL_ONGROUND;
}

static void PredEffectPush(PredWorld& w, int entIdx, const PredSimEnt& e, PredPlayerIO& io)
{
	float push[3] = { e.speed * e.pushDir[0], e.speed * e.pushDir[1], e.speed * e.pushDir[2] };

	if (e.spawnflags & 0x80) {
		for (int c = 0; c < 3; ++c) io.velocity[c] += push[c];
		if (push[2] > 0.0f) *io.flags &= ~FL_ONGROUND;
		w.ents[entIdx].disabled = 1;
		return;
	}

	if (*io.flags & FL_BASEVELOCITY)
		for (int c = 0; c < 3; ++c) push[c] += io.baseVelocity[c];

	if (push[2] > 0.0f && (*io.flags & FL_ONGROUND)) {
		*io.flags &= ~FL_ONGROUND;
		io.origin[2] += 1.0f;
	}
	for (int c = 0; c < 3; ++c) io.baseVelocity[c] = push[c];
	*io.flags |= FL_BASEVELOCITY;
}

static void PredEffectMulti(PredMap& map, PredWorld& w, int entIdx, const PredSimEnt& e, float curtime)
{
	PredEntState& st = w.ents[entIdx];
	if (curtime < st.multiWaitUntil) return;
	PredFireOutput(map, w, entIdx, OUT_ONTRIGGER, 0.0f, curtime);
	if (e.wait > 0.0f) st.multiWaitUntil = curtime + e.wait;
	else               st.disabled = 1;
}

static bool PredPassesFilters(const PredMap& map, const PredWorld& w, const PredSimEnt& e, PredPlayerIO& io)
{
	int sf = e.spawnflags;
	bool client = (*io.flags & FL_CLIENT_BIT) != 0;
	bool pass = (sf & 0x40) || ((sf & 0x01) && client);
	if (!pass) return false;
	if (sf & 0x20) return false;
	return PredFilterPasses(map, e.filterIndex, w.playerName.c_str());
}

struct PredRuntime {
	PredMap* map = nullptr;
	PredWorld world;
	std::vector<PredWorld> ring;
	std::vector<int>       ringTick;
	int  prevTick = INT_MIN;
	bool initialized = false;

	bool  viewSnapPending = false;
	float viewSnapAngles[3] = {0,0,0};
};

static void PredRunTouch(PredRuntime& rt, PredPlayerIO& io)
{
	if (!rt.map) return;
	PredMap& map = *rt.map;
	PredWorld& w = rt.world;
	const float ct = io.curtime;
	rt.viewSnapPending = false;

	float queryOrigin[3] = { io.origin[0], io.origin[1], io.origin[2] };
	for (int i = 0; i < (int)map.sim.size(); ++i) {
		const PredSimEnt& e = map.sim[i];
		if (e.modelIndex < 0 || !PredIsTriggerClass(e.cls)) continue;
		PredEntState& st = w.ents[i];

		if (st.disabled) {
			if (st.playerInside) {
				PredFireOutput(map, w, i, OUT_ONENDTOUCH, 0.0f, ct);
				PredFireOutput(map, w, i, OUT_ONENDTOUCHALL, 0.0f, ct);
				st.playerInside = st.startTouchFired = 0;
			}
			continue;
		}

		bool overlap = PredBoxInModel(map, e.modelIndex, e.modelShift, queryOrigin, io.mins, io.maxs);
		bool passes = (e.cls == PC_GRAVITY) ? true : PredPassesFilters(map, w, e, io);

		if (overlap && passes) {
			if (!st.playerInside) {
				st.playerInside = st.startTouchFired = 1;
				PredFireOutput(map, w, i, OUT_ONSTARTTOUCH, 0.0f, ct);
				PredFireOutput(map, w, i, OUT_ONSTARTTOUCHALL, 0.0f, ct);
			}
			switch (e.cls) {
			case PC_TELEPORT:
				PredEffectTeleport(e, io);
				if (e.snapAngles) {
					rt.viewSnapPending = true;
					memcpy(rt.viewSnapAngles, e.destAngles, sizeof rt.viewSnapAngles);
				}
				break;
			case PC_PUSH:     PredEffectPush(w, i, e, io); break;
			case PC_GRAVITY:  *io.gravity = e.gravity; break;
			case PC_MULTIPLE:
			case PC_ONCE:     PredEffectMulti(map, w, i, e, ct); break;
			default: break;
			}
		} else if (st.playerInside && !overlap) {
			st.playerInside = st.startTouchFired = 0;
			PredFireOutput(map, w, i, OUT_ONENDTOUCH, 0.0f, ct);
			PredFireOutput(map, w, i, OUT_ONENDTOUCHALL, 0.0f, ct);
		}
	}

	PredServiceEvents(map, w, ct, io);
}

static void PredResetWorld(const PredMap& map, PredWorld& w)
{
	w.events.clear();
	w.playerName.clear();
	w.ents.assign(map.sim.size(), PredEntState());
	for (size_t i = 0; i < map.sim.size(); ++i) {
		w.ents[i].disabled = map.sim[i].initialDisabled ? 1 : 0;
		w.ents[i].counterValue = map.sim[i].initialCounter;
		w.ents[i].branchValue = map.sim[i].initialBranch ? 1 : 0;
	}
	w.actionTimesLeft.resize(map.actions.size());
	for (size_t i = 0; i < map.actions.size(); ++i)
		w.actionTimesLeft[i] = map.actions[i].timesToFire;
}

static void PredReset(PredRuntime& rt)
{
	delete rt.map;
	rt.map = nullptr;
	rt.ring.clear();
	rt.ringTick.clear();
	rt.prevTick = INT_MIN;
	rt.initialized = false;
}

static void PredAdopt(PredRuntime& rt, PredMap* fresh)
{
	delete rt.map;
	rt.map = fresh;
	rt.ring.assign(PRED_RING_SIZE, PredWorld());
	rt.ringTick.assign(PRED_RING_SIZE, INT_MIN);
	rt.prevTick = INT_MIN;
	rt.initialized = false;
	PredResetWorld(*fresh, rt.world);
}

static void PredCommandStart(PredRuntime& rt, int tick)
{
	if (!rt.map) return;
	bool newPass = tick <= rt.prevTick;
	if (newPass || !rt.initialized) {
		int prev = tick - 1;
		int slot = ((prev % PRED_RING_SIZE) + PRED_RING_SIZE) % PRED_RING_SIZE;
		if (rt.ringTick[slot] == prev) rt.world = rt.ring[slot];
		else PredResetWorld(*rt.map, rt.world);
	}
	rt.initialized = true;
}

static void PredCommandEnd(PredRuntime& rt, int tick)
{
	if (!rt.map) return;
	int slot = ((tick % PRED_RING_SIZE) + PRED_RING_SIZE) % PRED_RING_SIZE;
	rt.ring[slot] = rt.world;
	rt.ringTick[slot] = tick;
	rt.prevTick = tick;
}
