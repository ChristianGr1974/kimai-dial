#pragma once

#include <Arduino.h>
#include <vector>
#include "app_state.h"
#include "credentials_provider.h"

// Thin HTTP wrapper around the Kimai API. Each function returns its result
// via an out parameter and indicates success through its return value (HTTP
// 2xx + parsing ok). The Kimai credentials (base URL/token) come through an
// injected ICredentialsProvider instead of a direct SettingsStore
// dependency (dependency inversion, per the architecture spec) - reference,
// no heap allocation.
namespace KimaiClient {

bool fetchProjects(const ICredentialsProvider &credentials, std::vector<KimaiProject> &outProjects,
                    String &outError);

bool fetchActivities(const ICredentialsProvider &credentials, int projectId,
                      std::vector<KimaiActivity> &outActivities, String &outError);

// Query the active timesheet entry (resume after boot).
// outId = -1 if no active entry exists.
bool fetchActiveTimesheet(const ICredentialsProvider &credentials, int &outId, int &outProjectId,
                           int &outActivityId, String &outBegin, String &outError);

bool startTimesheet(const ICredentialsProvider &credentials, int projectId, int activityId, int &outId,
                     String &outError);

bool stopTimesheet(const ICredentialsProvider &credentials, int timesheetId, String &outError);

// Tests the PASSED-IN token (not the stored one) against
// GET /api/users/me. Returns true on HTTP 2xx. Used before saving a new
// token in TOKEN_VERIFY. The base URL still comes from the provider; the
// token is deliberately kept as an explicit parameter (the NEW, not yet
// saved token takes precedence over provider.kimaiToken()).
bool validateToken(const ICredentialsProvider &credentials, const String &token, String &outError);

} // namespace KimaiClient
