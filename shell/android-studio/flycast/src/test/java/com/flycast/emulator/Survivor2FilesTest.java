package com.flycast.emulator;

import static org.junit.Assert.*;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Synthetic ZIP headers exercise metadata checks without including game or BIOS data. */
public class Survivor2FilesTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();
    private File games, download;

    // Independent literals from the World BHF2 Ver. E definition in naomi_roms.cpp.
    private static final String[] NAMES = { "bhf2vere.2f", "bhf1ma2.4m", "bhf1ma3.4l", "bhf1ma4.4k", "bhf1ma5.4j", "bhf1ma6.4h", "bhf1ma7.4f", "bhf1ma8.4e", "bhf1ma9.4d", "bhf1ma10.4c", "bhf1ma11.4b", "bhf1ma12.6p", "bhf1ma13.6n", "bhf1ma14.6m", "bhf1ma15.6l" };
    private static final long[] CRCS = { 0x756e0de6L, 0x8073dec7L, 0x43cd16a4L, 0xf6aebed8L, 0xb5e1b582L, 0x345fd824L, 0x465ecff1L, 0x76c92354L, 0xd45a46eeL, 0x8c38d1f7L, 0xf49153c4L, 0x0e2bdd9aL, 0x055718adL, 0xd06c9bd7L, 0xdb3c396bL };

    @Before public void setup() throws IOException {
        games = tmp.newFolder("games");
        download = tmp.newFolder("Download");
    }
    private List<File> roots() { return Arrays.asList(games, download); }
    private static final class Entry {
        String name; long size, crc;
        Entry(String name, long size, long crc) { this.name = name; this.size = size; this.crc = crc; }
    }
    private List<Entry> cartridge() {
        List<Entry> entries = new ArrayList<>();
        for (int i = 0; i < NAMES.length; i++)
            entries.add(new Entry(NAMES[i], i == 0 ? 0x800000 : 0x1000000, CRCS[i]));
        return entries;
    }
    private Entry bios() { return new Entry("epr-21578h.ic27", 0x200000, 0x7b452946L); }
    private void bios(File dir) throws IOException { zip(new File(dir, "naomi.zip"), Arrays.asList(bios())); }
    private File complete(File dir) throws IOException {
        File game = new File(dir, "gunsur2.zip");
        zip(game, cartridge()); bios(dir); return game;
    }
    private void blocked(File game) {
        assertNull("Invalid explicit launch must not choose another set", Survivor2Files.launchPath(game, roots()));
        assertFalse(Survivor2StartStatus.from(Survivor2Files.find(Arrays.asList(game.getParentFile())), true).ready);
    }

    @Test public void completeWorldCartridgeAndBiosLaunchWithoutDisc() throws IOException {
        File game = complete(download);
        assertEquals(game.getAbsolutePath(), Survivor2Files.launchPath(null, roots()));
        assertTrue(Survivor2Files.find(roots()).missing.toString(), Survivor2Files.find(roots()).missing.isEmpty());
    }
    @Test public void missingGameAndBiosBlockPlay() {
        assertNull(Survivor2Files.launchPath(null, roots()));
        assertEquals(2, Survivor2Files.find(roots()).missing.size());
    }
    @Test public void missingBiosBlocksCompleteCartridge() throws IOException {
        File game = new File(download, "gunsur2.zip"); zip(game, cartridge()); blocked(game);
    }
    @Test public void biosMayBeEmbeddedInGameArchive() throws IOException {
        List<Entry> entries = cartridge(); entries.add(bios());
        File game = new File(download, "gunsur2.zip"); zip(game, entries);
        assertEquals(game.getAbsolutePath(), Survivor2Files.launchPath(game, roots()));
    }
    @Test public void malformedArchiveBlocksPlay() throws IOException {
        File game = new File(download, "gunsur2.zip");
        try (FileOutputStream out = new FileOutputStream(game)) { out.write(new byte[] {1, 2, 3, 4}); }
        bios(download); blocked(game);
    }
    @Test public void emptyArchiveBlocksPlay() throws IOException {
        File game = new File(download, "gunsur2.zip"); assertTrue(game.createNewFile());
        bios(download); blocked(game);
    }
    @Test public void missingOneCartridgeChipBlocksPlay() throws IOException {
        List<Entry> entries = cartridge(); entries.remove(11);
        File game = new File(download, "gunsur2.zip"); zip(game, entries); bios(download); blocked(game);
    }
    @Test public void expectedNameWithWrongSizeBlocksPlay() throws IOException {
        List<Entry> entries = cartridge(); entries.get(0).size = 1;
        File game = new File(download, "gunsur2.zip"); zip(game, entries); bios(download); blocked(game);
    }
    @Test public void expectedNameWithWrongCrcBlocksPlay() throws IOException {
        List<Entry> entries = cartridge(); entries.get(0).crc = 123L; // Unknown revision.
        File game = new File(download, "gunsur2.zip"); zip(game, entries); bios(download); blocked(game);
    }
    @Test public void renamedEntryWithMatchingCrcAndSizeUsesNativeLookup() throws IOException {
        List<Entry> entries = cartridge(); entries.get(0).name = "different-name.bin";
        File game = new File(download, "gunsur2.zip"); zip(game, entries); bios(download);
        assertEquals(game.getAbsolutePath(), Survivor2Files.launchPath(game, roots()));
    }
    @Test public void invalidRequestedSetNeverFallsThroughToCompleteSet() throws IOException {
        complete(games);
        File bad = new File(download, "gunsur2.zip"); zip(bad, Arrays.asList(bios()));
        assertNull(Survivor2Files.launchPath(bad, roots()));
    }
    @Test public void regionalCloneNeedsAnExplicitlySupportedRevision() throws IOException {
        File parent = complete(download);
        File clone = new File(download, "gunsur2j.zip"); zip(clone, cartridge());
        assertNull(Survivor2Files.launchPath(clone, roots()));
        assertEquals(parent.getAbsolutePath(), Survivor2Files.launchPath(null, roots()));
    }
    @Test public void cloneOnlyExplainsWorldRequirement() throws IOException {
        zip(new File(download, "gunsur2j.zip"), cartridge()); bios(download);
        assertNull(Survivor2Files.launchPath(null, roots()));
        assertTrue(Survivor2Files.find(roots()).missing.toString(),
                Survivor2Files.find(roots()).missing.get(0).contains("World"));
    }
    @Test public void unsupportedBiosCrcBlocksPlayEvenWithExpectedName() throws IOException {
        File game = complete(download);
        Entry wrong = bios(); wrong.crc = 123;
        zip(new File(download, "naomi.zip"), Arrays.asList(wrong)); blocked(game);
    }
    @Test public void wrongBiosSizeBlocksPlay() throws IOException {
        File game = complete(download);
        Entry wrong = bios(); wrong.size = 1;
        zip(new File(download, "naomi.zip"), Arrays.asList(wrong)); blocked(game);
    }
    @Test public void supportedBiosMayBeRenamedByItsCrc() throws IOException {
        File game = complete(download);
        Entry renamed = bios(); renamed.name = "renamed-bios.bin";
        zip(new File(download, "naomi.zip"), Arrays.asList(renamed));
        assertEquals(game.getAbsolutePath(), Survivor2Files.launchPath(game, roots()));
    }
    @Test public void completeNestedSetWinsOverIncompleteEarlierRoot() throws IOException {
        zip(new File(games, "gunsur2.zip"), Arrays.asList(bios()));
        File nested = new File(download, "Survivor2"); assertTrue(nested.mkdir());
        File game = complete(nested);
        assertEquals(game.getAbsolutePath(), Survivor2Files.launchPath(null, roots()));
    }
    @Test public void readyStatusHasTwoMarksAndOwnStorageNeedsNoAccess() throws IOException {
        complete(games);
        Survivor2StartStatus s = Survivor2StartStatus.from(Survivor2Files.find(roots()), false);
        assertTrue(s.ready); assertEquals("READY", s.badge);
        assertEquals(Arrays.asList("gunsur2.zip ✓", "naomi.zip ✓"), s.fileMarks);
        assertTrue(s.missing.isEmpty());
    }
    @Test public void missingWithoutSharedStorageAccessAsksForAccess() {
        Survivor2StartStatus s = Survivor2StartStatus.from(Survivor2Files.find(roots()), false);
        assertFalse(s.ready); assertEquals("ALLOW ACCESS", s.badge);
    }
    @Test public void initialStatusDoesNotEnablePlay() {
        Survivor2StartStatus s = Survivor2StartStatus.from(null, true);
        assertFalse(s.ready); assertEquals("CHECKING", s.badge); assertTrue(s.fileMarks.isEmpty());
    }

    @Test public void missingFilesInstructionsPointToDownload() {
        Survivor2Files.Result r = Survivor2Files.find(roots());
        assertTrue(r.missing.get(0), r.missing.get(0).contains("in Download"));
        assertTrue(r.missing.get(1), r.missing.get(1).contains("in Download"));
    }
    @Test public void missingBiosStatusNamesOnlyBios() throws IOException {
        zip(new File(download, "gunsur2.zip"), cartridge());
        Survivor2StartStatus s = Survivor2StartStatus.from(Survivor2Files.find(roots()), true);
        assertFalse(s.ready); assertEquals("MISSING FILES", s.badge);
        assertEquals(Arrays.asList("gunsur2.zip ✓", "naomi.zip missing"), s.fileMarks);
        assertEquals(1, s.missing.size()); assertTrue(s.missing.get(0).startsWith("naomi.zip missing"));
    }
    @Test public void malformedBiosBlocksPlay() throws IOException {
        File game = complete(download);
        try (FileOutputStream out = new FileOutputStream(new File(download, "naomi.zip"))) { out.write(1); }
        blocked(game);
    }
    @Test public void emptyCentralDirectoryBlocksPlay() throws IOException {
        File game = new File(download, "gunsur2.zip"); zip(game, new ArrayList<>()); bios(download); blocked(game);
    }
    @Test public void truncatedArchiveBlocksPlay() throws IOException {
        File game = complete(download);
        try (java.io.RandomAccessFile out = new java.io.RandomAccessFile(game, "rw")) { out.setLength(game.length() - 12); }
        blocked(game);
    }
    @Test public void invalidDuplicateCrcDoesNotShadowValidChip() throws IOException {
        List<Entry> entries = cartridge(); entries.add(0, new Entry("bad-first.bin", 1, 0x756e0de6L));
        File game = new File(download, "gunsur2.zip"); zip(game, entries); bios(download); blocked(game);
    }
    @Test public void unrelatedExplicitGameCannotUseFoundWorldSet() throws IOException {
        complete(games);
        File other = new File(download, "confmiss.zip"); zip(other, cartridge());
        assertNull(Survivor2Files.launchPath(other, roots()));
    }
    @Test public void versionBadgeKeepsMajorMinor() {
        assertEquals("v1.0", Survivor2StartStatus.shortVersion("1.0.0-gabc123"));
        assertEquals("v10.4", Survivor2StartStatus.shortVersion("10.4.2-gabc-vr"));
        assertEquals("", Survivor2StartStatus.shortVersion(null));
        assertEquals("", Survivor2StartStatus.shortVersion("gabc123"));
    }

    @Test public void explicitNoncanonicalNameCannotSubstituteCanonicalSet() throws IOException {
        complete(download);
        assertNull(Survivor2Files.launchPath(new File(download, "GUNSUR2.ZIP"), roots()));
    }
    @Test public void invalidEarlierBiosFilenameCannotShadowValidLaterRevision() throws IOException {
        File game = complete(download);
        zip(new File(download, "naomi.zip"), Arrays.asList(
                new Entry("epr-21577h.ic27", 0x200000, 123),
                new Entry("epr-21577g.ic27", 0x200000, 0x25f64af7L)));
        blocked(game);
    }
    @Test public void invalidEmbeddedBiosCannotShadowSeparateValidRevision() throws IOException {
        List<Entry> entries = cartridge(); entries.add(new Entry("epr-21577h.ic27", 0x200000, 123));
        File game = new File(download, "gunsur2.zip"); zip(game, entries);
        zip(new File(download, "naomi.zip"), Arrays.asList(new Entry("epr-21577g.ic27", 0x200000, 0x25f64af7L)));
        blocked(game);
    }

    // Only the directory and local-header metadata are used by setup. The one-byte stand-in
    // payloads are deliberately not ROMs; these fixtures do not test full content integrity.
    private void zip(File file, List<Entry> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        List<Integer> offsets = new ArrayList<>();
        for (Entry e : entries) {
            offsets.add(bytes.size()); byte[] name = e.name.getBytes(StandardCharsets.US_ASCII);
            le(out, 0x04034b50, 4); le(out, 20, 2); le(out, 0, 2); le(out, 8, 2);
            le(out, 0, 4); le(out, e.crc, 4); le(out, 1, 4); le(out, e.size, 4);
            le(out, name.length, 2); le(out, 0, 2); out.write(name); out.write(0);
        }
        int central = bytes.size();
        for (int i = 0; i < entries.size(); i++) {
            Entry e = entries.get(i); byte[] name = e.name.getBytes(StandardCharsets.US_ASCII);
            le(out, 0x02014b50, 4); le(out, 20, 2); le(out, 20, 2); le(out, 0, 2); le(out, 8, 2);
            le(out, 0, 4); le(out, e.crc, 4); le(out, 1, 4); le(out, e.size, 4);
            le(out, name.length, 2); le(out, 0, 2); le(out, 0, 2); le(out, 0, 2);
            le(out, 0, 2); le(out, 0, 4); le(out, offsets.get(i), 4); out.write(name);
        }
        int length = bytes.size() - central;
        le(out, 0x06054b50, 4); le(out, 0, 2); le(out, 0, 2);
        le(out, entries.size(), 2); le(out, entries.size(), 2);
        le(out, length, 4); le(out, central, 4); le(out, 0, 2);
        try (FileOutputStream fileOut = new FileOutputStream(file)) { bytes.writeTo(fileOut); }
    }
    private void le(DataOutputStream out, long n, int length) throws IOException {
        for (int i = 0; i < length; i++) out.write((int) (n >>> (8 * i)) & 0xff);
    }
}
