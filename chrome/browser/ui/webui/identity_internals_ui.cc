// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/identity_internals_ui.h"

#include <set>
#include <string>

#include "base/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/identity/identity_api.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "google_apis/gaia/gaia_constants.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// Properties of the Javascript object representing a token.
const char kExtensionId[] = "extensionId";
const char kExtensionName[] = "extensionName";
const char kScopes[] = "scopes";
const char kStatus[] = "status";
const char kTokenExpirationTime[] = "expirationTime";
const char kTokenId[] = "tokenId";

// RevokeToken message parameter offsets.
const int kRevokeTokenExtensionOffset = 0;
const int kRevokeTokenTokenOffset = 1;

class IdentityInternalsTokenRevoker;

class IdentityInternalsUIMessageHandler : public content::WebUIMessageHandler {
 public:
  IdentityInternalsUIMessageHandler();
  virtual ~IdentityInternalsUIMessageHandler();

  // Ensures that a proper clean up happens after a token is revoked. That
  // includes deleting the |token_revoker|, removing the token from Identity API
  // cache and updating the UI that the token is gone.
  void OnTokenRevokerDone(IdentityInternalsTokenRevoker* token_revoker);

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE;

 private:
  const std::string GetExtensionName(
      const extensions::IdentityAPI::TokenCacheKey& token_cache_key);

  ListValue* GetScopes(
      const extensions::IdentityAPI::TokenCacheKey& token_cache_key);

  const base::string16 GetStatus(
      const extensions::IdentityTokenCacheValue& token_cache_value);

  const std::string GetExpirationTime(
      const extensions::IdentityTokenCacheValue& token_cache_value);

  DictionaryValue* GetInfoForToken(
      const extensions::IdentityAPI::TokenCacheKey& token_cache_key,
      const extensions::IdentityTokenCacheValue& token_cache_value);

  void GetInfoForAllTokens(const ListValue* args);

  // Initiates revoking of the token, based on the extension ID and token
  // passed as entries in the args list.
  void RevokeToken(const ListValue* args);

  // A vector of token revokers that are currently revoking tokens.
  ScopedVector<IdentityInternalsTokenRevoker> token_revokers_;
};

// Handles the revoking of an access token and helps performing the clean up
// after it is revoked by holding information about the access token and related
// extension ID.
class IdentityInternalsTokenRevoker : public GaiaAuthConsumer {
 public:
  // Revokes |access_token| from extension with |extension_id|.
  // |profile| is required for its request context. |consumer| will be
  // notified when revocation succeeds via |OnTokenRevokerDone()|.
  IdentityInternalsTokenRevoker(const std::string& extension_id,
                                const std::string& access_token,
                                Profile* profile,
                                IdentityInternalsUIMessageHandler* consumer);
  virtual ~IdentityInternalsTokenRevoker();

  // Returns the access token being revoked.
  const std::string& access_token() const { return access_token_; }

  // Returns the ID of the extension the access token is related to.
  const std::string& extension_id() const { return extension_id_; }

  // GaiaAuthConsumer implementation.
  virtual void OnOAuth2RevokeTokenCompleted() OVERRIDE;

 private:
  // An object used to start a token revoke request.
  GaiaAuthFetcher fetcher_;
  // An ID of an extension the access token is related to.
  const std::string extension_id_;
  // The access token to revoke.
  const std::string access_token_;
  // An object that needs to be notified once the access token is revoked.
  IdentityInternalsUIMessageHandler* consumer_;  // weak.

  DISALLOW_COPY_AND_ASSIGN(IdentityInternalsTokenRevoker);
};

IdentityInternalsUIMessageHandler::IdentityInternalsUIMessageHandler() {}

IdentityInternalsUIMessageHandler::~IdentityInternalsUIMessageHandler() {}

void IdentityInternalsUIMessageHandler::OnTokenRevokerDone(
    IdentityInternalsTokenRevoker* token_revoker) {
  // Remove token from the cache.
  extensions::IdentityAPI::GetFactoryInstance()->GetForProfile(
      Profile::FromWebUI(web_ui()))->EraseCachedToken(
          token_revoker->extension_id(), token_revoker->access_token());

  // Update view about the token being removed.
  ListValue result;
  result.AppendString(token_revoker->access_token());
  web_ui()->CallJavascriptFunction("identity_internals.tokenRevokeDone",
                                   result);

  // Erase the revoker.
  ScopedVector<IdentityInternalsTokenRevoker>::iterator iter =
      std::find(token_revokers_.begin(), token_revokers_.end(), token_revoker);
  DCHECK(iter != token_revokers_.end());
  token_revokers_.erase(iter);
}

const std::string IdentityInternalsUIMessageHandler::GetExtensionName(
    const extensions::IdentityAPI::TokenCacheKey& token_cache_key) {
  ExtensionService* extension_service = extensions::ExtensionSystem::Get(
      Profile::FromWebUI(web_ui()))->extension_service();
  const extensions::Extension* extension =
      extension_service->extensions()->GetByID(token_cache_key.extension_id);
  if (!extension)
    return std::string();
  return extension->name();
}

ListValue* IdentityInternalsUIMessageHandler::GetScopes(
    const extensions::IdentityAPI::TokenCacheKey& token_cache_key) {
  ListValue* scopes_value = new ListValue();
  for (std::set<std::string>::const_iterator
           iter = token_cache_key.scopes.begin();
       iter != token_cache_key.scopes.end(); ++iter) {
    scopes_value->AppendString(*iter);
  }
  return scopes_value;
}

const base::string16 IdentityInternalsUIMessageHandler::GetStatus(
    const extensions::IdentityTokenCacheValue& token_cache_value) {
  switch (token_cache_value.status()) {
    case extensions::IdentityTokenCacheValue::CACHE_STATUS_ADVICE:
      // Fallthrough to NOT FOUND case, as ADVICE is short lived.
    case extensions::IdentityTokenCacheValue::CACHE_STATUS_NOTFOUND:
      return l10n_util::GetStringUTF16(
          IDS_IDENTITY_INTERNALS_TOKEN_NOT_FOUND);
    case extensions::IdentityTokenCacheValue::CACHE_STATUS_TOKEN:
      return l10n_util::GetStringUTF16(
          IDS_IDENTITY_INTERNALS_TOKEN_PRESENT);
  }
  NOTREACHED();
  return base::string16();
}

const std::string IdentityInternalsUIMessageHandler::GetExpirationTime(
    const extensions::IdentityTokenCacheValue& token_cache_value) {
  return UTF16ToUTF8(base::TimeFormatFriendlyDateAndTime(
      token_cache_value.expiration_time()));
}

DictionaryValue* IdentityInternalsUIMessageHandler::GetInfoForToken(
    const extensions::IdentityAPI::TokenCacheKey& token_cache_key,
    const extensions::IdentityTokenCacheValue& token_cache_value) {
  DictionaryValue* token_data = new DictionaryValue();
  token_data->SetString(kExtensionId, token_cache_key.extension_id);
  token_data->SetString(kExtensionName, GetExtensionName(token_cache_key));
  token_data->Set(kScopes, GetScopes(token_cache_key));
  token_data->SetString(kStatus, GetStatus(token_cache_value));
  token_data->SetString(kTokenId, token_cache_value.token());
  token_data->SetString(kTokenExpirationTime,
                        GetExpirationTime(token_cache_value));
  return token_data;
}

void IdentityInternalsUIMessageHandler::GetInfoForAllTokens(
    const ListValue* args) {
  ListValue results;
  extensions::IdentityAPI::CachedTokens tokens =
      extensions::IdentityAPI::GetFactoryInstance()->GetForProfile(
          Profile::FromWebUI(web_ui()))->GetAllCachedTokens();
  for (extensions::IdentityAPI::CachedTokens::const_iterator
           iter = tokens.begin(); iter != tokens.end(); ++iter) {
    results.Append(GetInfoForToken(iter->first, iter->second));
  }

  web_ui()->CallJavascriptFunction("identity_internals.returnTokens", results);
}

void IdentityInternalsUIMessageHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("identityInternalsGetTokens",
      base::Bind(&IdentityInternalsUIMessageHandler::GetInfoForAllTokens,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("identityInternalsRevokeToken",
      base::Bind(&IdentityInternalsUIMessageHandler::RevokeToken,
                 base::Unretained(this)));
}

void IdentityInternalsUIMessageHandler::RevokeToken(const ListValue* args) {
  std::string extension_id;
  std::string access_token;
  args->GetString(kRevokeTokenExtensionOffset, &extension_id);
  args->GetString(kRevokeTokenTokenOffset, &access_token);
  token_revokers_.push_back(new IdentityInternalsTokenRevoker(
      extension_id, access_token, Profile::FromWebUI(web_ui()), this));
}

IdentityInternalsTokenRevoker::IdentityInternalsTokenRevoker(
    const std::string& extension_id,
    const std::string& access_token,
    Profile* profile,
    IdentityInternalsUIMessageHandler* consumer)
    : fetcher_(this, GaiaConstants::kChromeSource,
               profile->GetRequestContext()),
      extension_id_(extension_id),
      access_token_(access_token),
      consumer_(consumer) {
  DCHECK(consumer_);
  fetcher_.StartRevokeOAuth2Token(access_token);
}

IdentityInternalsTokenRevoker::~IdentityInternalsTokenRevoker() {}

void IdentityInternalsTokenRevoker::OnOAuth2RevokeTokenCompleted() {
  consumer_->OnTokenRevokerDone(this);
}

}  // namespace

IdentityInternalsUI::IdentityInternalsUI(content::WebUI* web_ui)
  : content::WebUIController(web_ui) {
  // chrome://identity-internals source.
  content::WebUIDataSource* html_source =
    content::WebUIDataSource::Create(chrome::kChromeUIIdentityInternalsHost);
  html_source->SetUseJsonJSFormatV2();

  // Localized strings
  html_source->AddLocalizedString("tokenCacheHeader",
      IDS_IDENTITY_INTERNALS_TOKEN_CACHE_TEXT);
  html_source->AddLocalizedString("tokenId",
      IDS_IDENTITY_INTERNALS_TOKEN_ID);
  html_source->AddLocalizedString("extensionName",
      IDS_IDENTITY_INTERNALS_EXTENSION_NAME);
  html_source->AddLocalizedString("extensionId",
      IDS_IDENTITY_INTERNALS_EXTENSION_ID);
  html_source->AddLocalizedString("tokenStatus",
      IDS_IDENTITY_INTERNALS_TOKEN_STATUS);
  html_source->AddLocalizedString("expirationTime",
      IDS_IDENTITY_INTERNALS_EXPIRATION_TIME);
  html_source->AddLocalizedString("scopes",
      IDS_IDENTITY_INTERNALS_SCOPES);
  html_source->AddLocalizedString("revoke",
      IDS_IDENTITY_INTERNALS_REVOKE);
  html_source->SetJsonPath("strings.js");

  // Required resources
  html_source->AddResourcePath("identity_internals.css",
      IDR_IDENTITY_INTERNALS_CSS);
  html_source->AddResourcePath("identity_internals.js",
      IDR_IDENTITY_INTERNALS_JS);
  html_source->SetDefaultResource(IDR_IDENTITY_INTERNALS_HTML);

  content::WebUIDataSource::Add(Profile::FromWebUI(web_ui), html_source);

  web_ui->AddMessageHandler(new IdentityInternalsUIMessageHandler());
}

IdentityInternalsUI::~IdentityInternalsUI() {}

