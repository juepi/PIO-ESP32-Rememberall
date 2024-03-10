########################################################################
##                Rememberall Feeder
##    https://github.com/juepi/PIO-ESP32-Rememberall
########################################################################
## This script takes ICS URLs/files as source to
## feed the Rememberall with display text and Reminders
## schedule this script to run every full hour
## it will find the next upcoming event of all configured
## calendars and prepares it for the Rememberall
## ATTENTION: Make sure to save this script UTF8-BOM encoded!
## Else "umlaut converter" (Convert-Trim-String) will probably not work!
#########################################################################
# This scipt requires:
# =====================
# IcalVCard https://afterlogic.com/mailbee-net/icalvcard / https://www.nuget.org/packages/ICalVCard
# M2Mqtt https://www.nuget.org/packages/M2Mqtt/
# Copy the DLL files to a "lib" subdirectory of $PSScriptRoot
# lastly, a local MQTT broker is also required of course..

Param(
    [parameter(Mandatory = $false)][switch]$ForceUpdate, # forces refresh of MQTT topics (also resets acknowledged events!); not needed for normal use
    [parameter(Mandatory = $false)][switch]$WhatIf # Do not publish to broker; for debugging purpose
)

# ===============================================================================
#region ================ Load external libraries ================================
# ===============================================================================

[System.Reflection.Assembly]::LoadFrom(("$($PSScriptRoot)\lib\ICalVCard.dll")) | Out-Null
[System.Reflection.Assembly]::LoadFrom(("$($PSScriptRoot)\lib\M2Mqtt.Net.dll")) | Out-Null
#endregion


# ===============================================================================
#region ================ Configuration Settings =================================
# ===============================================================================

# General config and Reminder settings
$Config = @{
    IcsDir              = "$($PSScriptRoot)\ics"; # Directory where local ICS files are stored
    CosyReminderPeriod  = 24; # cosy reminder 24hrs before the event starts (all-day-events start at 00:00, will be corrected to noon)
    AggroReminderPeriod = 12; # aggressive reminder 12hrs before event starts
    PreviewPeriod       = 24; # If no event is within PreviewPeriod + CosyReminderPeriod the Rememberall will sleep for PreviewPeriod hours
    maxCharsPerLine     = 10; # max amount of characters displayable in 1 ePaper line of the Rememberall
    maxLines            = 3; # max number of lines that can be displayed
    eventAckStr         = "ack"; # filtered string of t_Status; at match, current event has been acknowledged on the Rememberall
    ActiveReminderHours = [ordered]@{
        Start = @(5, 18); # Rememberall will be active during these times
        End   = @(7, 20)  # example: from 5:00 to 7:59 and 18:00 to 20:59; enter desired ranges **ascending**
    };
}

#MQTT Settings
$MQTT = @{}
$MQTT.Broker = "your-mqtt-broker"
$MQTT.ClientID = "$ENV:COMPUTERNAME"
$MQTT.TopicTree = "topic/tree/Rememberall"
$MQTT.t_Reminder = "$($MQTT.TopicTree)/eventReminder"
$MQTT.t_Txt = "$($MQTT.TopicTree)/eventTxt"
$MQTT.t_SleepUntil = "$($MQTT.TopicTree)/SleepUntil"
$MQTT.t_Status = "$($MQTT.TopicTree)/Status" # subscribed topic; if "ack", current event already acknowledged by user

# Filters and assigned LED-colors for Calendar Events
# will be filtered with -imatch from the "Summary" field of suitable events
# LEDColor = 0xRRGGBB ; use powerful colors, bright ones may look like white on the RGB LEDs (possibly due to low brightness / powered by 3.3V)
# TXTColor = 0 (black) or 1 (red)
$EventFilter = [ordered]@{
    Sports    = @{
        Regex     = @('Badminton|Klettern|Wandern'); # different keywords to search for category sport
        LEDColor  = @('0x00FF00'); # they will all get the same color assigned
        TXTColor  = @(1);
        TXTPrefix = @('0;Sport:'); # Text line prefixed to the first word of the summary text, starting with "TXTColor;"
        TXTSuffix = @('') # Text line suffixed
    };
    Household = @{
        Regex     = @('Biomüll', 'Restmüll', 'gelber Sack'); # you can have multiple types in a category
        LEDColor  = @('0x00FF00', '0x0000FF', '0xFFFF00'); # with different colors/prefixes/suffixes assigned
        TXTColor  = @(1, 1, 1);
        TXTPrefix = @('0;Tonne' , '0;Tonne', '');
        TXTSuffix = @('0;raus!', '0;raus!', '0;raus!')
    };
    Birthdays = @{
        Regex     = @('Geburtstag');
        LEDColor  = @('0xFF7A01');
        TXTColor  = @(1);
        TXTPrefix = @('0;Geburtstag');
        TXTSuffix = @('')
    };
    Health    = @{
        Regex     = @('Physio|Arzt|Massage');
        LEDColor  = @('0xFF0000');
        TXTColor  = @(1);
        TXTPrefix = @('1;Termin:');
        TXTSuffix = @('')
    };
    Others    = @{ # NOTE: This must be the LAST category in the EventFilter hashtable!
        Regex     = @('.*'); # Fallback for events where no other category matches
        LEDColor  = @('0xFF00FF');
        TXTColor  = @(1);
        TXTPrefix = @('');
        TXTSuffix = @('')
    };
}

# ICS Calendar sources
$Calendars = [ordered]@{
    gcal    = @{
        URL = "https://calendar.google.com/calendar/ical/xy%40gmail.com/private-xxx/basic.ics" #private google calendar URLs can be obtained in the google calendar settings
    };
    bdays = @{
        File = "$($Config.IcsDir)\bdays.ics"
    };
}
#endregion


# ===============================================================================
# region =================== MQTT Client Setup ==================================
# ===============================================================================

$MqttClient = [uPLibrary.Networking.M2Mqtt.MqttClient]($MQTT.Broker)
if ($($MqttClient.Connect($MQTT.ClientID)) -ne 0 ) {
    Write-Error "Failed to connect to MQTT broker!" -ErrorAction Stop
}

# Register EventHandler for receiving messages for subscribed topics
Register-ObjectEvent `
    -inputObject $MqttClient `
    -EventName MqttMsgPublishReceived `
    -Action { MQTTMsgReceived $($args[1]) } | Out-Null

# Subscribe to the eventTxt topic (used to compare if updating MQTT topics is necessary)
$MqttClient.Subscribe("$($MQTT.t_Txt)", 0) | Out-Null # Subscribe with QoS 0
$MqttClient.Subscribe("$($MQTT.t_Status)", 0) | Out-Null
# Global variables for received eventTXT and Status messages
$Global:MqttTxtTopicMessage = "none"
$Global:MqttStatusTopicMessage = "none"
#endregion


# ===============================================================================
#region ========================= Functions =====================================
# ===============================================================================

# MQTT Callback function for subscribed topics (only 1 in this case)
function global:MQTTMsgReceived {
    Param(
        [parameter(Mandatory = $true)]$MqttRcv
    )
    $topic = $MqttRcv.topic
    switch ($topic) {
        $MQTT.t_Txt { $Global:MqttTxtTopicMessage = $([System.Text.Encoding]::ASCII.GetString($MqttRcv.Message)) }
        $MQTT.t_Status { $Global:MqttStatusTopicMessage = $([System.Text.Encoding]::ASCII.GetString($MqttRcv.Message)) }
    }
}

# Extracts infos from given iCal Event and returns a hashtable with all extracted data as well as the MQTT eventTxt and eventReminder messages
function Get-EventInfo {
    Param
    (
        [Parameter(Mandatory = $true, Position = 1)]
        $Event,
        [Parameter(Mandatory = $true, Position = 2)]
        $EventFilter
    )
    # Precreate resulting Hashtable
    $EventInfo = @{
        Text     = @{
            Msg = ''   # resulting text message to be sent to Rememberall
        };
        Reminder = @{
            Deadline      = '';
            CosyReminder  = '';
            AggroReminder = '';
            LEDColor      = '';
            Msg           = ''    # resulting reminder message to be sent to Rememberall
        };
    }

    # Delay start for all-day-events (like birthdays) to noon
    if ($Event.IsAllDay) {
        $EventStart = $Event.Start.AddHours(12) 
    }
    else {
        $EventStart = $Event.Start
    }
    $EventInfo.Reminder.Deadline = ([long]([Math]::Round(($EventStart - [DateTime]::Now).TotalSeconds, 0) + ([DateTimeOffset]::Now.ToUnixTimeSeconds()))).ToString("x")
    $EventInfo.Reminder.CosyReminder = ([long]([Math]::Round(($EventStart.AddHours(-$Config.CosyReminderPeriod) - [DateTime]::Now).TotalSeconds, 0) + ([DateTimeOffset]::Now.ToUnixTimeSeconds()))).ToString("x")
    $EventInfo.Reminder.AggroReminder = ([long]([Math]::Round(($EventStart.AddHours(-$Config.AggroReminderPeriod) - [DateTime]::Now).TotalSeconds, 0) + ([DateTimeOffset]::Now.ToUnixTimeSeconds()))).ToString("x")

    # Filter Summary field of event to get category and text to display as well as the LED-color
    foreach ($Category in $EventFilter.Keys) {
        #  Loop through categories until we find a Regex match
        for ($i = 0; $i -lt $EventFilter.$Category.Regex.Count ; $i++) {
            if ($Event.Summary -imatch $EventFilter.$Category.Regex[$i]) {
                # match found, fill Text Lines
                $TextLines = 0
                $HasPrefix = $false
                $HasSuffix = $false

                if ($EventFilter.$Category.TXTPrefix[$i].Length -gt 0) {
                    $HasPrefix = $true
                    $TextLines++

                }
                if ($EventFilter.$Category.TXTSuffix[$i].Length -gt 0) {
                    $HasSuffix = $true
                    $TextLines++

                }

                # Start creating the text string with a placeholder for the number of lines
                $EventInfo.Text.Msg = "LC_Ph"
                if ($HasPrefix) {
                    $EventInfo.Text.Msg = $EventInfo.Text.Msg + "|" + $(Convert-Trim-String $EventFilter.$Category.TXTPrefix[$i] -Length ($Config.maxCharsPerLine + 2)) # TXTPrefix also contains color, thus +2 chars
                }

                # Add allowed/available amount of words of the Events Summary field (one word per line)
                $SummaryWordCount = $Event.Summary.Split(" ").Count
                for ($word = 0; $word -lt $SummaryWordCount; $word++) {
                    $EventInfo.Text.Msg = $EventInfo.Text.Msg + "|" + $($EventFilter.$Category.TXTColor[$i]) + ";" + $(Convert-Trim-String $Event.Summary.Split(" ")[$word] -Length $Config.maxCharsPerLine)
                    $TextLines++
                    if ($TextLines -eq $Config.maxLines) {
                        # All lines filled
                        break
                    }
                }
                
                if ($HasSuffix) {
                    $EventInfo.Text.Msg = $EventInfo.Text.Msg + "|" + $(Convert-Trim-String $EventFilter.$Category.TXTSuffix[$i] -Length ($Config.maxCharsPerLine + 2)) # TXTSuffix also contains color, thus +2 chars
                }
                # Replace Linecount placeholder to finish the final text message for Rememberall
                $EventInfo.Text.Msg = $EventInfo.Text.Msg.replace('LC_Ph', "$($TextLines)")
                # Get desired LED color
                $EventInfo.Reminder.LEDColor = $EventFilter.$Category.LEDColor[$i]
                # Build final Reminder message for Rememberall
                $EventInfo.Reminder.Msg = ($EventInfo.Reminder.Deadline + "|" + $EventInfo.Reminder.CosyReminder + "|" + $EventInfo.Reminder.AggroReminder + "|" + $EventInfo.Reminder.LEDColor).ToString()
                # Done, return EventInfo hashtable
                return $EventInfo
            }
        }
    }
    # something probably went wrong, return empty EventInfo hashtable..
    return $EventInfo
}

# Calculates Unix epoch (+ offset in seconds) and returns it in hexadecimal base
function Get-EpochFromNow {
    param (
        $AddSeconds = 0
    )
    return ([DateTimeOffset]::Now.ToUnixTimeSeconds() + $AddSeconds).ToString("x")
}

# Helper to convert german umlauts, as the Adafruit GFX library font used in the Rememberall is limited to 7 bit ASCII
# afterwards trims the resulting string to x characters (usually 10 allowed per ePaper display line)
function Convert-Trim-String {
    param (
        [parameter(Mandatory = $True, Position = 1)] [string] $Str
        , [parameter(Mandatory = $True, Position = 2)] [int] $Length
    )
    $Str = $Str -creplace 'Ü', 'Ue' -creplace 'Ö', 'Oe' -creplace 'Ä', 'Ae' -creplace 'ü', 'ue' -creplace 'ö', 'oe' -creplace 'ä', 'ae' -replace 'ß', 'ss'
    return $Str[0..($Length - 1)] -join ""
}

# Returns a SleepUntil Epoch time according to Config
function Calculate-SleepUntil {
    Param
    (
        [Parameter(Mandatory = $true, Position = 1)]
        $Config,
        $ActiveEvent = $false
    )    

    # If there's an active event, check if we're within a ActiveReminderPeriod
    if ($ActiveEvent) {
        for ($i = 0 ; $i -lt $Config.ActiveReminderHours.Start.Count ; $i++) {
            if ((Get-Date).Hour -ge $Config.ActiveReminderHours.Start[$i] -and (Get-Date).Hour -le $Config.ActiveReminderHours.End[$i]) {
                # within ActiveReminderPeriod and active event -> Rememberall must be awake
                return 0
            }
        }
    }
    
    # Identify start of next ActiveReminderPeriod
    for ($i = 0 ; $i -lt $Config.ActiveReminderHours.Start.Count ; $i++) {
        $SleepSeconds = [int]([Math]::Round((New-TimeSpan -Start (Get-Date) -End (Get-Date -Hour $Config.ActiveReminderHours.Start[$i] -Minute 0 -Second 0)).TotalSeconds, 0))
        if ( $SleepSeconds -lt 0) {
            # TimeSpan negative -> Start time in the past
            continue
        }
        else {
            # Value valid, calculate epoch and return
            return $(Get-EpochFromNow -AddSeconds $SleepSeconds)
        }
    }
    # next ActiveReminderPeriod is tomorrow, use first available start time
    $SleepSeconds = [int]([Math]::Round((New-TimeSpan -Start (Get-Date) -End (Get-Date -Hour $Config.ActiveReminderHours.Start[0] -Minute 0 -Second 0).AddDays(1)).TotalSeconds, 0))
    return $(Get-EpochFromNow -AddSeconds $SleepSeconds)
}
#endregion


# =======================================================================
#region =================== Main Script =================================
# =======================================================================
Clear-Variable NextEventCandidates -ErrorAction SilentlyContinue
# Declare empty array for next event candidates
$NextEventCandidates = @()

ForEach ($CalName in $Calendars.Keys) {
    # Fetch from URL (if available)
    if ( $Calendars.$CalName.URL.Length -gt 0 ) {
        try { $Calendar = [iCal.iCalendar]::LoadFromUri($Calendars.$CalName.URL) }
        catch { Write-Error "Failed to fetch ical from URL $($Calendars.$CalName.URL)" -ErrorAction Stop }
    }
    else {
        try { $Calendar = [iCal.iCalendar]::LoadFromFile($Calendars.$CalName.File, [Text.Encoding]::UTF8) }
        catch { Write-Error "Failed to fetch ical from file $($Calendars.$CalName.File)" -ErrorAction Stop }
    }

    # Fetch regular events within the next PreviewPeriod+CosyReminderPeriod from current calendar..
    $CheckPeriod = $Config.PreviewPeriod + $Config.CosyReminderPeriod
    $ThisCalRegularEvents = $Calendar.Events | Where-Object { $_.DTStart.Local -gt [DateTime]::Now -and $_.DTStart.Local -lt [DateTime]::Now.AddHours($CheckPeriod) }
    # Save all events as possible candidates to a simplified Hashtable
    if ($ThisCalRegularEvents.Count -gt 0) {
        ForEach ($Event in $ThisCalRegularEvents) {
            $tmpEvtInfo = @{}
            $tmpEvtInfo.Summary = $($Event.Summary)
            $tmpEvtInfo.Start = $Event.DTStart.Local
            $tmpEvtInfo.IsAllDay = [bool]$Event.IsAllDay
            $NextEventCandidates += $tmpEvtInfo
        }
    }
    
    # Now check for upcoming recurring events in the calendar
    $ThisCalRecEvents = $Calendar.Events | Where-Object { $_.RecurrenceRules.Count -gt 0 }
    $ThisCalRecEvents = $ThisCalRecEvents | Where-Object { $_.GetOccurrences([iCal.iCalDateTime]::Now, [iCal.iCalDateTime]::Now.AddHours($CheckPeriod)).Period.StartTime.Count -gt 0 }
    # ..and save the candidates
    if ($ThisCalRecEvents.Count -gt 0) {
        ForEach ($Event in $ThisCalRecEvents) {
            $tmpEvtInfo = @{}
            $tmpEvtInfo.Summary = $($Event.Summary)
            # ATTN: if there are multiple occurences within PreviewPeriod+CosyReminderPeriod for a single recurring event, only the first one will be used!
            $tmpEvtInfo.Start = $Event.GetOccurrences([iCal.iCalDateTime]::Now, [iCal.iCalDateTime]::Now.AddHours($CheckPeriod)).Period.StartTime[0].Local
            $tmpEvtInfo.IsAllDay = [bool]$Event.IsAllDay
            $NextEventCandidates += $tmpEvtInfo
        }
    }
}

# Check if there is a candidate
if ($NextEventCandidates.Count -gt 0) {
    # pick the final earliest event from the candidates
    $NextEvent = $NextEventCandidates[0]
    for ($i = 1 ; $i -lt $NextEventCandidates.Count; $i++) {
        if ($NextEvent.Start -gt $NextEventCandidates[$i].Start) {
            $NextEvent = $NextEventCandidates[$i]
        }
    }

    Write-Host "Handling Event [$($NextEvent.Summary)] starting at $($NextEvent.Start)" -ForegroundColor Green

    # Check if the event is within the Cosy reminder period
    if ($NextEvent.Start -lt [DateTime]::Now.AddHours($Config.CosyReminderPeriod)) {
        #Filter category and extract required text from events "Summary" field
        $SendMe = Get-EventInfo -Event $NextEvent -EventFilter $EventFilter
        # Verify we've got valid results
        if (-not ($SendMe.Text.Msg.Length -gt 0 -and $SendMe.Reminder.Msg.Length -gt 0)) {
            Write-Error "Invalid results returned from Get-EventInfo!" -ErrorAction Stop
        }
        # Only send if update is needed (new event) or ForceUpdate param set $true
        if ($Global:MqttTxtTopicMessage -ne $SendMe.Text.Msg -or $ForceUpdate) {
            if (-not $WhatIf) {
                $MqttClient.Publish($MQTT.t_Txt, [System.Text.Encoding]::ASCII.GetBytes($SendMe.Text.Msg), 1, 1) | Out-Null # Publish with QoS 1 and Retained
                Write-Host "Text message sent to broker: $($SendMe.Text.Msg)"
                $MqttClient.Publish($MQTT.t_Reminder, [System.Text.Encoding]::ASCII.GetBytes($SendMe.Reminder.Msg), 1, 1) | Out-Null
                Write-Host "Reminder message sent to broker: $($SendMe.Reminder.Msg)"
                $MqttClient.Publish($MQTT.t_Status, [System.Text.Encoding]::ASCII.GetBytes("newEvent"), 1, 1) | Out-Null
                Write-Host "Status message reset to newEvent."
            }
            else {
                Write-Host "WHATIF: Send Text message to broker: $($SendMe.Text.Msg)" -ForegroundColor Magenta
                Write-Host "WHATIF: Send Reminder message to broker: $($SendMe.Reminder.Msg)" -ForegroundColor Magenta
                Write-Host "WHATIF: Reset Status message to newEvent." -ForegroundColor Magenta
            }
            $EventIsActive = $true
        }
        else {
            Write-Host "MQTT event topics already up-to-date, check if acknowledged by user.." -ForegroundColor Green
            if ($Global:MqttStatusTopicMessage -eq $Config.eventAckStr) {
                Write-Host "The current event has been acknowledged on the Rememberall. Ignoring this event." -ForegroundColor Yellow
                $EventIsActive = $false
            }
            else {
                Write-Host "No." -ForegroundColor Green
                $EventIsActive = $true
            }
        }
        # Finally, handle SleepUntil for Rememberall
        $SleepUntil = $(Calculate-SleepUntil -Config $Config -ActiveEvent $EventIsActive)
        if (-not $WhatIf) {
            $MqttClient.Publish($MQTT.t_SleepUntil, [System.Text.Encoding]::ASCII.GetBytes($SleepUntil), 1, 1) | Out-Null
            Write-Host "SleepUntil message sent to broker: $($SleepUntil)" -ForegroundColor Yellow
        }
        else {
            Write-Host "WHATIF: Send SleepUntil message to broker: $($SleepUntil)" -ForegroundColor Magenta
        }
    }
    else {
        # The event is further in the future, sleep until the next ActiveReminderPeriod
        $SleepUntil = $(Calculate-SleepUntil -Config $Config -ActiveEvent $false)
        if (-not $WhatIf) {
            $MqttClient.Publish($MQTT.t_SleepUntil, [System.Text.Encoding]::ASCII.GetBytes($SleepUntil), 1, 1) | Out-Null
            Write-Host "No active event, SleepUntil message sent to broker: $($SleepUntil)" -ForegroundColor Yellow
        }
        else {
            Write-Host "WHATIF: No active event, send SleepUntil message to broker: $($SleepUntil)" -ForegroundColor Magenta
        }
    }
}
else {
    Write-Host "No upcoming events, sleeping for $($Config.PreviewPeriod) hours" -ForegroundColor Yellow
    $SleepUntil = $(Get-EpochFromNow -AddSeconds $($Config.PreviewPeriod * 3600))
    if (-not $WhatIf) {
        $MqttClient.Publish($MQTT.t_SleepUntil, [System.Text.Encoding]::ASCII.GetBytes($SleepUntil), 1, 1) | Out-Null
        Write-Host "No active event, SleepUntil message sent to broker: $($SleepUntil)" -ForegroundColor Yellow
    }
    else {
        Write-Host "WHATIF: No active event, send SleepUntil message to broker: $($SleepUntil)" -ForegroundColor Magenta
    }
}

# Wait a bit before disconnecting to ensure that the MQTT topics have been sent
Start-Sleep -Seconds 2
$MqttClient.Disconnect()
Write-Host "Finished." -ForegroundColor Green
#endregion
