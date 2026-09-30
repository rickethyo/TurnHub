package com.turnhub.android.data

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONException
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** A firmware version as major.minor.patch; suffixes such as `-dev` are ignored. */
data class FirmwareVersion(val major: Int, val minor: Int, val patch: Int) : Comparable<FirmwareVersion> {
    override fun compareTo(other: FirmwareVersion): Int =
        compareValuesBy(this, other, FirmwareVersion::major, FirmwareVersion::minor, FirmwareVersion::patch)

    override fun toString() = "$major.$minor.$patch"

    companion object {
        private val PATTERN = Regex("^(\\d+)\\.(\\d+)\\.(\\d+)")

        fun parse(text: String): FirmwareVersion? = PATTERN.find(text.trim())?.destructured?.let { (a, b, c) ->
            FirmwareVersion(a.toInt(), b.toInt(), c.toInt())
        }
    }
}

/** The products in a release, as the feed and the `.thfw` header name them. */
enum class FirmwareProduct(val wire: String, val label: String) {
    ATLAS("atlas", "Atlas"),
    SIGIL_EINK("sigil-eink", "E-ink Sigil"),
    SIGIL_OLED("sigil-oled", "OLED Sigil"),
    ;

    companion object {
        fun fromWire(value: String): FirmwareProduct? = entries.firstOrNull { it.wire == value }
    }
}

/** One signed package in the release feed. [sha256] is lowercase hex of the whole `.thfw` file. */
data class FirmwarePackage(
    val product: FirmwareProduct,
    val version: FirmwareVersion,
    val file: String,
    val size: Long,
    val sha256: String,
)

/**
 * `turnhub-firmware.json` (Documentation/engineering/SIGIL_OTA.md, "Release
 * feed"). Unknown products and fields are ignored so later releases can add
 * them; a package with a bad version, size or hash is dropped, never guessed.
 */
data class FirmwareReleaseFeed(val release: String, val packages: List<FirmwarePackage>) {

    fun packageFor(product: FirmwareProduct): FirmwarePackage? = packages.firstOrNull { it.product == product }

    companion object {
        const val SCHEMA = 1
        private val SHA256 = Regex("^[0-9a-f]{64}$")
        private val FILE = Regex("^[A-Za-z0-9._-]{1,96}\\.thfw$")

        /** Throws [IllegalArgumentException] for a feed this app can't read. */
        fun parse(body: String): FirmwareReleaseFeed {
            val root = try {
                JSONObject(body)
            } catch (e: JSONException) {
                throw IllegalArgumentException("The release feed is not JSON")
            }
            require(root.optInt("schema", -1) == SCHEMA) { "Unsupported release feed schema ${root.opt("schema")}" }
            val list = root.optJSONArray("packages") ?: throw IllegalArgumentException("The release feed lists no packages")
            val packages = (0 until list.length()).mapNotNull { i ->
                val entry = list.optJSONObject(i) ?: return@mapNotNull null
                val product = FirmwareProduct.fromWire(entry.optString("product")) ?: return@mapNotNull null
                val version = FirmwareVersion.parse(entry.optString("version")) ?: return@mapNotNull null
                val file = entry.optString("file").takeIf { FILE.matches(it) } ?: return@mapNotNull null
                val size = entry.optLong("size", -1).takeIf { it in 1..MAX_PACKAGE_BYTES } ?: return@mapNotNull null
                val sha = entry.optString("sha256").lowercase().takeIf { SHA256.matches(it) } ?: return@mapNotNull null
                FirmwarePackage(product, version, file, size, sha)
            }
            return FirmwareReleaseFeed(root.optString("release"), packages)
        }

        /** Larger than any app slot on Atlas or a Sigil (about 1.9 MB). */
        const val MAX_PACKAGE_BYTES = 4L * 1024 * 1024
    }
}

/** A device the setup update step lists: Atlas, or one paired Sigil. */
data class UpdateTarget(
    /** Null for Atlas, otherwise the Sigil's ID on Atlas. */
    val sigilId: Int?,
    val label: String,
    val product: FirmwareProduct,
    /** What the device reports, or null when it doesn't (then it is offered the release). */
    val running: FirmwareVersion?,
    val available: FirmwarePackage?,
    /** Atlas can only update a Sigil it hears from. */
    val online: Boolean = true,
) {
    val needsUpdate: Boolean get() = available != null && (running == null || available.version > running)
}

/**
 * What one update prompt would do. Atlas goes first, then each Sigil; only
 * devices with a newer package are updated, and devices the release has no
 * package for are listed as up to date.
 */
data class UpdatePlan(val targets: List<UpdateTarget>) {
    val pending: List<UpdateTarget> get() = targets.filter { it.needsUpdate }
    val anyUpdate: Boolean get() = pending.isNotEmpty()

    companion object {
        fun build(
            feed: FirmwareReleaseFeed,
            atlasFirmware: String,
            sigils: List<DeviceInfo>,
        ): UpdatePlan {
            val atlas = UpdateTarget(
                sigilId = null,
                label = "Atlas",
                product = FirmwareProduct.ATLAS,
                running = FirmwareVersion.parse(atlasFirmware),
                available = feed.packageFor(FirmwareProduct.ATLAS),
            )
            // Test harness boards never take an update; a Sigil whose display
            // Atlas doesn't know yet can't be matched to a package.
            val sigilTargets = sigils.filter { !it.isHarness }.sortedBy { it.id }.map { device ->
                val product = when (device.display) {
                    "oled" -> FirmwareProduct.SIGIL_OLED
                    "epaper" -> FirmwareProduct.SIGIL_EINK
                    else -> null
                }
                UpdateTarget(
                    sigilId = device.id,
                    label = device.label.ifBlank { "Sigil ${device.id + 1}" },
                    product = product ?: FirmwareProduct.SIGIL_EINK,
                    running = FirmwareVersion.parse(device.firmware),
                    available = product?.let(feed::packageFor),
                    online = device.online,
                )
            }
            return UpdatePlan(listOf(atlas) + sigilTargets)
        }
    }
}

/** Where releases come from. Tests substitute a fake; production reads GitHub. */
interface FirmwareReleaseSource {
    /** The latest feed; throws [IOException] or [IllegalArgumentException]. */
    suspend fun latestFeed(): FirmwareReleaseFeed

    /** The package's bytes, checked against the feed's size and SHA-256; throws on any mismatch. */
    suspend fun download(pkg: FirmwarePackage, onProgress: (Long) -> Unit = {}): ByteArray
}

/**
 * GitHub Releases on the public TurnHub repository (owner, 2026-09-30: GitHub
 * hosts the firmware "at least for now"). No token is needed. Requests use the
 * phone's default network: the Atlas Wi-Fi joined through a network specifier
 * is not the default, so the phone's own internet carries these.
 */
class GitHubFirmwareReleases(
    private val baseUrl: String = DEFAULT_BASE_URL,
    private val opener: HttpConnectionOpener = HttpConnectionOpener.Default,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : FirmwareReleaseSource {

    override suspend fun latestFeed(): FirmwareReleaseFeed {
        val body = fetch("$baseUrl/$FEED_FILE", MAX_FEED_BYTES) {}
        return FirmwareReleaseFeed.parse(body.toString(Charsets.UTF_8))
    }

    override suspend fun download(pkg: FirmwarePackage, onProgress: (Long) -> Unit): ByteArray {
        val bytes = fetch("$baseUrl/${pkg.file}", pkg.size, onProgress)
        verifyPackage(pkg, bytes)
        return bytes
    }

    private suspend fun fetch(url: String, limit: Long, onProgress: (Long) -> Unit): ByteArray =
        withContext(ioDispatcher) {
            val connection = opener.open(URL(url))
            try {
                connection.connectTimeout = 10_000
                connection.readTimeout = 30_000
                connection.useCaches = false
                // GitHub answers release downloads with a redirect to its file host.
                connection.instanceFollowRedirects = true
                val code = connection.responseCode
                if (code == HttpURLConnection.HTTP_NOT_FOUND) throw NoReleaseException()
                if (code !in 200..299) throw IOException("GitHub answered HTTP $code")
                val out = ByteArrayOutputStream()
                connection.inputStream.use { input ->
                    val buffer = ByteArray(16 * 1024)
                    while (true) {
                        val read = input.read(buffer)
                        if (read < 0) break
                        out.write(buffer, 0, read)
                        if (out.size() > limit) throw IOException("The download is larger than the release says")
                        onProgress(out.size().toLong())
                    }
                }
                out.toByteArray()
            } finally {
                connection.disconnect()
            }
        }

    companion object {
        const val DEFAULT_BASE_URL = "https://github.com/rickethyo/TurnHub/releases/latest/download"
        const val FEED_FILE = "turnhub-firmware.json"
        private const val MAX_FEED_BYTES = 64L * 1024
    }
}

/** No release has been published yet (the feed is missing). */
class NoReleaseException : IOException("No firmware release has been published yet")

/** Throws [IOException] unless [bytes] match the feed entry exactly. */
fun verifyPackage(pkg: FirmwarePackage, bytes: ByteArray) {
    if (bytes.size.toLong() != pkg.size) {
        throw IOException("${pkg.file} is ${bytes.size} bytes, the release says ${pkg.size}")
    }
    val digest = MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
    if (digest != pkg.sha256) throw IOException("${pkg.file} doesn't match the release's checksum")
}
