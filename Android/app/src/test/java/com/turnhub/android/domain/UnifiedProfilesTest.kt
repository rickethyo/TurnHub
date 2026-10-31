package com.turnhub.android.domain
import com.turnhub.android.protocol.ProfileSummary
import com.turnhub.android.standalone.*
import org.junit.Assert.*
import org.junit.Test
class UnifiedProfilesTest {
 @Test fun `same names are not merged identities`() {
  val rows=unifiedProfiles(LocalLibrary(players=listOf(SavedLocalPlayer("local","Sam",profileId="LOCAL"))),"table",listOf(ProfileSummary("REMOTE","Sam",true)))
  assertEquals(2,rows.size)
  assertEquals(setOf("This device","Atlas"),rows.map{it.location}.toSet())
 }
 @Test fun `same identity and explicitly linked identities occupy one row`() {
  val local=SavedLocalPlayer("local","Sam",profileId="LOCAL",links=listOf(AtlasProfileLink("table","REMOTE")))
  val rows=unifiedProfiles(LocalLibrary(players=listOf(local)),"table",listOf(ProfileSummary("REMOTE","Sam",true)))
  assertEquals(1,rows.size);assertEquals("This device · Atlas",rows.single().location)
  assertEquals(1,unifiedProfiles(LocalLibrary(players=listOf(local)),"other",listOf(ProfileSummary("LOCAL","Sam",false))).size)
 }
}
