// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/onc/onc_merger.h"

#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/values.h"
#include "chromeos/network/onc/onc_constants.h"
#include "chromeos/network/onc/onc_signature.h"

namespace chromeos {
namespace onc {
namespace {

typedef scoped_ptr<base::DictionaryValue> DictionaryPtr;

// Inserts |true| at every field name in |result| that is recommended in
// |policy|.
void MarkRecommendedFieldnames(const base::DictionaryValue& policy,
                               base::DictionaryValue* result) {
  const ListValue* recommended_value = NULL;
  if (!policy.GetListWithoutPathExpansion(kRecommended, &recommended_value))
    return;
  for (ListValue::const_iterator it = recommended_value->begin();
       it != recommended_value->end(); ++it) {
    std::string entry;
    if ((*it)->GetAsString(&entry))
      result->SetBooleanWithoutPathExpansion(entry, true);
  }
}

// Returns a dictionary which contains |true| at each path that is editable by
// the user. No other fields are set.
DictionaryPtr GetEditableFlags(const base::DictionaryValue& policy) {
  DictionaryPtr result_editable(new base::DictionaryValue);
  MarkRecommendedFieldnames(policy, result_editable.get());

  // Recurse into nested dictionaries.
  for (base::DictionaryValue::Iterator it(policy); !it.IsAtEnd();
       it.Advance()) {
    const base::DictionaryValue* child_policy = NULL;
    if (it.key() == kRecommended ||
        !it.value().GetAsDictionary(&child_policy)) {
      continue;
    }

    result_editable->SetWithoutPathExpansion(
        it.key(), GetEditableFlags(*child_policy).release());
  }
  return result_editable.Pass();
}

// This is the base class for merging a list of DictionaryValues in
// parallel. See MergeDictionaries function.
class MergeListOfDictionaries {
 public:
  typedef std::vector<const base::DictionaryValue*> DictPtrs;

  MergeListOfDictionaries() {
  }

  virtual ~MergeListOfDictionaries() {
  }

  // For each path in any of the dictionaries |dicts|, the function
  // MergeListOfValues is called with the list of values that are located at
  // that path in each of the dictionaries. This function returns a new
  // dictionary containing all results of MergeListOfValues at the respective
  // paths. The resulting dictionary doesn't contain empty dictionaries.
  DictionaryPtr MergeDictionaries(const DictPtrs &dicts) {
    DictionaryPtr result(new base::DictionaryValue);
    std::set<std::string> visited;
    for (DictPtrs::const_iterator it_outer = dicts.begin();
         it_outer != dicts.end(); ++it_outer) {
      if (!*it_outer)
        continue;

      for (base::DictionaryValue::Iterator field(**it_outer); !field.IsAtEnd();
           field.Advance()) {
        const std::string& key = field.key();
        if (key == kRecommended || !visited.insert(key).second)
          continue;

        scoped_ptr<base::Value> merged_value;
        if (field.value().IsType(base::Value::TYPE_DICTIONARY)) {
          DictPtrs nested_dicts;
          for (DictPtrs::const_iterator it_inner = dicts.begin();
               it_inner != dicts.end(); ++it_inner) {
            const base::DictionaryValue* nested_dict = NULL;
            if (*it_inner)
              (*it_inner)->GetDictionaryWithoutPathExpansion(key, &nested_dict);
            nested_dicts.push_back(nested_dict);
          }
          DictionaryPtr merged_dict(MergeNestedDictionaries(key, nested_dicts));
          if (!merged_dict->empty())
            merged_value = merged_dict.Pass();
        } else {
          std::vector<const base::Value*> values;
          for (DictPtrs::const_iterator it_inner = dicts.begin();
               it_inner != dicts.end(); ++it_inner) {
            const base::Value* value = NULL;
            if (*it_inner)
              (*it_inner)->GetWithoutPathExpansion(key, &value);
            values.push_back(value);
          }
          merged_value = MergeListOfValues(key, values);
        }

        if (merged_value)
          result->SetWithoutPathExpansion(key, merged_value.release());
      }
    }
    return result.Pass();
  }

 protected:
  // This function is called by MergeDictionaries for each list of values that
  // are located at the same path in each of the dictionaries. The order of the
  // values is the same as of the given dictionaries |dicts|. If a dictionary
  // doesn't contain a path then it's value is NULL.
  virtual scoped_ptr<base::Value> MergeListOfValues(
      const std::string& key,
      const std::vector<const base::Value*>& values) = 0;

  virtual DictionaryPtr MergeNestedDictionaries(const std::string& key,
                                                const DictPtrs &dicts) {
    return MergeDictionaries(dicts);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MergeListOfDictionaries);
};

// This is the base class for merging policies and user settings.
class MergeSettingsAndPolicies : public MergeListOfDictionaries {
 public:
  struct ValueParams {
    const base::Value* user_policy;
    const base::Value* device_policy;
    const base::Value* user_setting;
    const base::Value* shared_setting;
    const base::Value* active_setting;
    bool user_editable;
    bool device_editable;
  };

  MergeSettingsAndPolicies() {}

  // Merge the provided dictionaries. For each path in any of the dictionaries,
  // MergeValues is called. Its results are collected in a new dictionary which
  // is then returned. The resulting dictionary never contains empty
  // dictionaries.
  DictionaryPtr MergeDictionaries(
      const base::DictionaryValue* user_policy,
      const base::DictionaryValue* device_policy,
      const base::DictionaryValue* user_settings,
      const base::DictionaryValue* shared_settings,
      const base::DictionaryValue* active_settings) {
    hasUserPolicy_ = (user_policy != NULL);
    hasDevicePolicy_ = (device_policy != NULL);

    DictionaryPtr user_editable;
    if (user_policy != NULL)
      user_editable = GetEditableFlags(*user_policy);

    DictionaryPtr device_editable;
    if (device_policy != NULL)
      device_editable = GetEditableFlags(*device_policy);

    std::vector<const base::DictionaryValue*> dicts(kLastIndex, NULL);
    dicts[kUserPolicyIndex] = user_policy;
    dicts[kDevicePolicyIndex] = device_policy;
    dicts[kUserSettingsIndex] = user_settings;
    dicts[kSharedSettingsIndex] = shared_settings;
    dicts[kActiveSettingsIndex] = active_settings;
    dicts[kUserEditableIndex] = user_editable.get();
    dicts[kDeviceEditableIndex] = device_editable.get();
    return MergeListOfDictionaries::MergeDictionaries(dicts);
  }

 protected:
  // This function is called by MergeDictionaries for each list of values that
  // are located at the same path in each of the dictionaries. Implementations
  // can use the Has*Policy functions.
  virtual scoped_ptr<base::Value> MergeValues(const std::string& key,
                                              const ValueParams& values) = 0;

  // Whether a user policy was provided.
  bool HasUserPolicy() {
    return hasUserPolicy_;
  }

  // Whether a device policy was provided.
  bool HasDevicePolicy() {
    return hasDevicePolicy_;
  }

  // MergeListOfDictionaries override.
  virtual scoped_ptr<base::Value> MergeListOfValues(
      const std::string& key,
      const std::vector<const base::Value*>& values) OVERRIDE {
    bool user_editable = !HasUserPolicy();
    if (values[kUserEditableIndex])
      values[kUserEditableIndex]->GetAsBoolean(&user_editable);

    bool device_editable = !HasDevicePolicy();
    if (values[kDeviceEditableIndex])
      values[kDeviceEditableIndex]->GetAsBoolean(&device_editable);

    ValueParams params;
    params.user_policy = values[kUserPolicyIndex];
    params.device_policy = values[kDevicePolicyIndex];
    params.user_setting = values[kUserSettingsIndex];
    params.shared_setting = values[kSharedSettingsIndex];
    params.active_setting = values[kActiveSettingsIndex];
    params.user_editable = user_editable;
    params.device_editable = device_editable;
    return MergeValues(key, params);
  }

 private:
  enum {
    kUserPolicyIndex,
    kDevicePolicyIndex,
    kUserSettingsIndex,
    kSharedSettingsIndex,
    kActiveSettingsIndex,
    kUserEditableIndex,
    kDeviceEditableIndex,
    kLastIndex
  };

  bool hasUserPolicy_, hasDevicePolicy_;

  DISALLOW_COPY_AND_ASSIGN(MergeSettingsAndPolicies);
};

// Call MergeDictionaries to merge policies and settings to the effective
// values. This ignores the active settings of Shill. See the description of
// MergeSettingsAndPoliciesToEffective.
class MergeToEffective : public MergeSettingsAndPolicies {
 public:
  MergeToEffective() {}

 protected:
  // Merges |values| to the effective value (Mandatory policy overwrites user
  // settings overwrites shared settings overwrites recommended policy). |which|
  // is set to the respective onc::kAugmentation* constant that indicates which
  // source of settings is effective. Note that this function may return a NULL
  // pointer and set |which| to kAugmentationUserPolicy, which means that the
  // user policy didn't set a value but also didn't recommend it, thus enforcing
  // the empty value.
  scoped_ptr<base::Value> MergeValues(const std::string& key,
                                      const ValueParams& values,
                                      std::string* which) {
    const base::Value* result = NULL;
    which->clear();
    if (!values.user_editable) {
      result = values.user_policy;
      *which = kAugmentationUserPolicy;
    } else if (!values.device_editable) {
      result = values.device_policy;
      *which = kAugmentationDevicePolicy;
    } else if (values.user_setting) {
      result = values.user_setting;
      *which = kAugmentationUserSetting;
    } else if (values.shared_setting) {
      result = values.shared_setting;
      *which = kAugmentationSharedSetting;
    } else if (values.user_policy) {
      result = values.user_policy;
      *which = kAugmentationUserPolicy;
    } else if (values.device_policy) {
      result = values.device_policy;
      *which = kAugmentationDevicePolicy;
    } else {
      // Can be reached if the current field is recommended, but none of the
      // dictionaries contained a value for it.
    }
    if (result)
      return make_scoped_ptr(result->DeepCopy());
    return scoped_ptr<base::Value>();
  }

  // MergeSettingsAndPolicies override.
  virtual scoped_ptr<base::Value> MergeValues(
      const std::string& key,
      const ValueParams& values) OVERRIDE {
    std::string which;
    return MergeValues(key, values, &which);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MergeToEffective);
};

// Call MergeDictionaries to merge policies and settings to an augmented
// dictionary which contains a dictionary for each value in the original
// dictionaries. See the description of MergeSettingsAndPoliciesToAugmented.
class MergeToAugmented : public MergeToEffective {
 public:
  MergeToAugmented() {}

  DictionaryPtr MergeDictionaries(
      const OncValueSignature& signature,
      const base::DictionaryValue* user_policy,
      const base::DictionaryValue* device_policy,
      const base::DictionaryValue* user_settings,
      const base::DictionaryValue* shared_settings,
      const base::DictionaryValue* active_settings) {
    signature_ = &signature;
    return MergeToEffective::MergeDictionaries(user_policy,
                                               device_policy,
                                               user_settings,
                                               shared_settings,
                                               active_settings);
  }

 protected:
  // MergeSettingsAndPolicies override.
  virtual scoped_ptr<base::Value> MergeValues(
      const std::string& key,
      const ValueParams& values) OVERRIDE {
    scoped_ptr<base::DictionaryValue> result(new base::DictionaryValue);
    if (values.active_setting) {
      result->SetWithoutPathExpansion(kAugmentationActiveSetting,
                                      values.active_setting->DeepCopy());
    }

    const OncFieldSignature* field = NULL;
    if (signature_)
      field = GetFieldSignature(*signature_, key);

    if (field) {
      // This field is part of the provided ONCSignature, thus it can be
      // controlled by policy.
      std::string which_effective;
      MergeToEffective::MergeValues(key, values, &which_effective).reset();
      if (!which_effective.empty()) {
        result->SetStringWithoutPathExpansion(kAugmentationEffectiveSetting,
                                              which_effective);
      }
      bool is_credential = onc::FieldIsCredential(*signature_, key);

      // Prevent credentials from being forwarded in cleartext to
      // UI. User/shared credentials are not stored separately, so they cannot
      // leak here.
      if (!is_credential) {
        if (values.user_policy) {
          result->SetWithoutPathExpansion(kAugmentationUserPolicy,
                                          values.user_policy->DeepCopy());
        }
        if (values.device_policy) {
          result->SetWithoutPathExpansion(kAugmentationDevicePolicy,
                                          values.device_policy->DeepCopy());
        }
      }
      if (values.user_setting) {
        result->SetWithoutPathExpansion(kAugmentationUserSetting,
                                        values.user_setting->DeepCopy());
      }
      if (values.shared_setting) {
        result->SetWithoutPathExpansion(kAugmentationSharedSetting,
                                        values.shared_setting->DeepCopy());
      }
      if (HasUserPolicy() && values.user_editable) {
        result->SetBooleanWithoutPathExpansion(kAugmentationUserEditable,
                                               true);
      }
      if (HasDevicePolicy() && values.device_editable) {
        result->SetBooleanWithoutPathExpansion(kAugmentationDeviceEditable,
                                               true);
      }
    } else {
      // This field is not part of the provided ONCSignature, thus it cannot be
      // controlled by policy.
      result->SetStringWithoutPathExpansion(kAugmentationEffectiveSetting,
                                            kAugmentationUnmanaged);
    }
    if (result->empty())
      result.reset();
    return result.PassAs<base::Value>();
  }

  // MergeListOfDictionaries override.
  virtual DictionaryPtr MergeNestedDictionaries(
      const std::string& key,
      const DictPtrs &dicts) OVERRIDE {
    DictionaryPtr result;
    if (signature_) {
      const OncValueSignature* enclosing_signature = signature_;
      signature_ = NULL;

      const OncFieldSignature* field =
          GetFieldSignature(*enclosing_signature, key);
      if (field)
        signature_ = field->value_signature;
      result = MergeToEffective::MergeNestedDictionaries(key, dicts);

      signature_ = enclosing_signature;
    } else {
      result = MergeToEffective::MergeNestedDictionaries(key, dicts);
    }
    return result.Pass();
  }

 private:
  const OncValueSignature* signature_;
  DISALLOW_COPY_AND_ASSIGN(MergeToAugmented);
};

}  // namespace

DictionaryPtr MergeSettingsAndPoliciesToEffective(
    const base::DictionaryValue* user_policy,
    const base::DictionaryValue* device_policy,
    const base::DictionaryValue* user_settings,
    const base::DictionaryValue* shared_settings) {
  MergeToEffective merger;
  return merger.MergeDictionaries(
      user_policy, device_policy, user_settings, shared_settings, NULL);
}

DictionaryPtr MergeSettingsAndPoliciesToAugmented(
    const OncValueSignature& signature,
    const base::DictionaryValue* user_policy,
    const base::DictionaryValue* device_policy,
    const base::DictionaryValue* user_settings,
    const base::DictionaryValue* shared_settings,
    const base::DictionaryValue* active_settings) {
  MergeToAugmented merger;
  return merger.MergeDictionaries(
      signature, user_policy, device_policy, user_settings, shared_settings,
      active_settings);
}

}  // namespace onc
}  // namespace chromeos
